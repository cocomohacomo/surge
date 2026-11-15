#include "surge/scope_validator.hpp"
#include "surge/sha256.hpp"
#include <algorithm>
#include <cctype>

namespace surge {
namespace {
bool anchor_valid(const ScopeAnchor& a,const ScopeSpanProposal& span,const std::string& source){
  return !a.anchor_id.empty() && a.source_digest==span.source_digest &&
    a.offset_basis==span.offset_basis && !a.packet_ref.empty() &&
    a.start_offset<a.end_offset && a.end_offset<=source.size() &&
    source.compare(a.start_offset,a.end_offset-a.start_offset,a.exact_text)==0 &&
    !a.exact_text.empty();
}
bool has_packet(const ScopeSpanProposal& span,const std::string& packet){
  return std::find(span.packet_refs.begin(),span.packet_refs.end(),packet)!=span.packet_refs.end();
}
bool target_kind(const ScopeSpanProposal& span,const std::map<std::string,GraphNode>& nodes,
                 std::size_t count,NodeKind kind){
  if(span.target_refs.size()!=count)return false;
  for(const auto& ref:span.target_refs)
    if(!nodes.count(ref)||nodes.at(ref).kind!=kind)return false;
  return true;
}
ScopeValidationResult reject(const char* error){
  ScopeValidationResult r;r.error=error;return r;
}
ScopeValidationResult unresolved(ScopeEffect effect,const char* reason){
  ScopeValidationResult r;r.accepted=true;r.status=ScopeStatus::Unresolved;r.effect=effect;r.error=reason;return r;
}
ScopeValidationResult closed(ScopeEffect effect,bool unconditional=false){
  ScopeValidationResult r;r.accepted=true;r.status=ScopeStatus::Closed;r.effect=effect;
  r.may_emit_unconditional_claim=unconditional;return r;
}
}  // namespace

ScopeValidationResult ScopeSpanValidator::validate(
    const ScopeSpanProposal& s,const std::string& source,
    const std::map<std::string,GraphNode>& nodes){
  const bool digest_hex=s.source_digest.size()==64 &&
    std::all_of(s.source_digest.begin(),s.source_digest.end(),[](unsigned char c){return std::isxdigit(c)!=0;});
  if(s.span_id.empty()||!digest_hex||s.offset_basis!="UTF8_BYTE_NFC"||
     s.packet_refs.empty())return reject("SCOPE_SOURCE_IDENTITY_INVALID");
  if(!is_valid_utf8(source)||sha256_hex(source)!=s.source_digest)
    return reject("SCOPE_SOURCE_DIGEST_MISMATCH");
  if(!anchor_valid(s.opener,s,source)||!has_packet(s,s.opener.packet_ref))
    return reject("SCOPE_OPENER_ANCHOR_INVALID");
  if(s.closer){
    if(s.closer->anchor_id==s.opener.anchor_id ||
       !anchor_valid(*s.closer,s,source)||!has_packet(s,s.closer->packet_ref)||
       s.closer->start_offset<s.opener.end_offset)
      return reject("SCOPE_CLOSER_ANCHOR_INVALID");
  }
  auto all_targets_exist=[&](){
    for(const auto& ref:s.target_refs)if(!nodes.count(ref))return false;
    return true;
  };
  if(!all_targets_exist())return reject("SCOPE_TARGET_NOT_FOUND");
  switch(s.kind){
    case ScopeKind::Negation:
      if(!target_kind(s,nodes,1,NodeKind::Claim))return reject("NEGATION_TARGET_INVALID");
      return closed(ScopeEffect::NegatedClaim,false);
    case ScopeKind::Condition:
      if(!target_kind(s,nodes,2,NodeKind::Claim))return reject("CONDITION_TARGETS_INVALID");
      if(!s.closer)return unresolved(ScopeEffect::ConditionalRelation,"CONDITION_SCOPE_OPEN");
      return closed(ScopeEffect::ConditionalRelation,false);
    case ScopeKind::Modality:
      if(!target_kind(s,nodes,1,NodeKind::Claim)||s.modality_kind.empty())
        return reject("MODALITY_TARGET_OR_KIND_INVALID");
      return closed(ScopeEffect::ModalityQualified,false);
    case ScopeKind::Concession:
      if(!target_kind(s,nodes,2,NodeKind::Claim))return reject("CONCESSION_TARGETS_INVALID");
      if(!s.closer)return unresolved(ScopeEffect::ConcessionRelation,"CONCESSION_SCOPE_OPEN");
      return closed(ScopeEffect::ConcessionRelation,false);
    case ScopeKind::Contrast:
      if(!target_kind(s,nodes,2,NodeKind::Claim))return reject("CONTRAST_TARGETS_INVALID");
      if(!s.closer)return unresolved(ScopeEffect::ContrastRelation,"CONTRAST_SCOPE_OPEN");
      return closed(ScopeEffect::ContrastRelation,false);
    case ScopeKind::Coreference:
    case ScopeKind::Anaphora:
      if(s.target_refs.size()!=1||s.antecedent_refs.empty())return reject("REFERENCE_SCOPE_INVALID");
      for(const auto& ref:s.antecedent_refs)
        if(!nodes.count(ref)||nodes.at(ref).kind!=NodeKind::Entity)return reject("ANTECEDENT_CANDIDATE_INVALID");
      if(s.antecedent_refs.size()!=1)return unresolved(ScopeEffect::ReferenceCandidate,"ANTECEDENT_AMBIGUOUS");
      return closed(ScopeEffect::ReferenceCandidate,false);
    case ScopeKind::Quotation:
      if(!s.closer)return unresolved(ScopeEffect::QuotationEvidence,"QUOTATION_SCOPE_OPEN");
      if(!s.attribution_ref.empty()&&(!nodes.count(s.attribution_ref)||nodes.at(s.attribution_ref).kind!=NodeKind::Entity))
        return reject("QUOTE_ATTRIBUTION_INVALID");
      return closed(ScopeEffect::QuotationEvidence,false);
    case ScopeKind::Temporal:
      if(!target_kind(s,nodes,1,NodeKind::Claim)||s.temporal_role.empty())
        return reject("TEMPORAL_TARGET_OR_ROLE_INVALID");
      return closed(ScopeEffect::TemporalQualifier,false);
    case ScopeKind::ListContinuation:
      if(s.parent_ref.empty())return reject("LIST_PARENT_REF_MISSING");
      return closed(ScopeEffect::StructuralContinuation,false);
    case ScopeKind::TableHeader:
      if(s.parent_ref.empty())return reject("TABLE_HEADER_REF_MISSING");
      return closed(ScopeEffect::TableHeaderScope,false);
  }
  return reject("SCOPE_KIND_UNKNOWN");
}
}  // namespace surge
