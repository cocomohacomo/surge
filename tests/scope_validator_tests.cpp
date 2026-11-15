#include "surge/scope_validator.hpp"
#include <cassert>
#include <iostream>
using namespace surge;
static ScopeAnchor anchor(const std::string& id,const std::string& digest,const std::string& text,
                          std::size_t start,const std::string& packet){
  return {id,digest,"UTF8_BYTE_NFC",start,start+text.size(),text,packet};
}
int main(){
  const std::string source="not approve if funded may approve although delayed but approve he said "yes" in 2026 item header";
  const std::string digest(64,'a');
  std::map<std::string,GraphNode> nodes={
    {"c1",{"c1",NodeKind::Claim,"claim1",{"e1"}}},
    {"c2",{"c2",NodeKind::Claim,"claim2",{"e1"}}},
    {"e1",{"e1",NodeKind::Evidence,"evidence",{}}},
    {"entity1",{"entity1",NodeKind::Entity,"entity",{}}}
  };
  auto make=[&](ScopeKind kind,const std::string& cue,std::size_t pos){
    ScopeSpanProposal s;s.span_id="s";s.kind=kind;s.source_digest=digest;s.offset_basis="UTF8_BYTE_NFC";
    s.opener=anchor("open",digest,cue,pos,"p1");s.packet_refs={"p1","p2"};return s;
  };
  {
    auto s=make(ScopeKind::Negation,"not",0);s.target_refs={"c1"};
    auto r=ScopeSpanValidator::validate(s,source,nodes);
    assert(r.accepted&&r.status==ScopeStatus::Closed&&r.effect==ScopeEffect::NegatedClaim&&!r.may_emit_unconditional_claim);
  }
  {
    auto s=make(ScopeKind::Condition,"if",14);s.target_refs={"c1","c2"};
    auto open=ScopeSpanValidator::validate(s,source,nodes);
    assert(open.accepted&&open.status==ScopeStatus::Unresolved);
    s.closer=anchor("close",digest,"approve",22,"p2");
    auto closed_result=ScopeSpanValidator::validate(s,source,nodes);
    assert(closed_result.accepted&&closed_result.status==ScopeStatus::Closed&&!closed_result.may_emit_unconditional_claim);
  }
  {
    auto s=make(ScopeKind::Modality,"may",25);s.target_refs={"c1"};s.modality_kind="POSSIBILITY";
    assert(ScopeSpanValidator::validate(s,source,nodes).effect==ScopeEffect::ModalityQualified);
  }
  {
    auto s=make(ScopeKind::Concession,"although",35);s.target_refs={"c1","c2"};
    s.closer=anchor("close",digest,"approve",48,"p2");
    assert(ScopeSpanValidator::validate(s,source,nodes).status==ScopeStatus::Closed);
  }
  {
    auto s=make(ScopeKind::Contrast,"but",52);s.target_refs={"c1","c2"};
    s.closer=anchor("close",digest,"approve",56,"p2");
    assert(ScopeSpanValidator::validate(s,source,nodes).effect==ScopeEffect::ContrastRelation);
  }
  {
    auto s=make(ScopeKind::Coreference,"he",63);s.target_refs={"c1"};s.antecedent_refs={"entity1"};
    assert(ScopeSpanValidator::validate(s,source,nodes).status==ScopeStatus::Closed);
    s.antecedent_refs={"entity1","c2"};
    assert(ScopeSpanValidator::validate(s,source,nodes).error=="ANTECEDENT_CANDIDATE_INVALID");
  }
  {
    auto s=make(ScopeKind::Anaphora,"he",63);s.target_refs={"c1"};s.antecedent_refs={"entity1","entity2"};
    nodes["entity2"]={"entity2",NodeKind::Entity,"entity2",{}};
    assert(ScopeSpanValidator::validate(s,source,nodes).status==ScopeStatus::Unresolved);
  }
  {
    auto s=make(ScopeKind::Quotation,"said",66);
    s.closer=anchor("close",digest,"yes",74,"p2");
    s.attribution_ref="entity1";
    assert(ScopeSpanValidator::validate(s,source,nodes).effect==ScopeEffect::QuotationEvidence);
  }
  {
    auto s=make(ScopeKind::Temporal,"in",78);s.target_refs={"c1"};s.temporal_role="VALID_FROM";
    assert(ScopeSpanValidator::validate(s,source,nodes).effect==ScopeEffect::TemporalQualifier);
  }
  {
    auto s=make(ScopeKind::ListContinuation,"item",87);s.parent_ref="list-1";
    assert(ScopeSpanValidator::validate(s,source,nodes).effect==ScopeEffect::StructuralContinuation);
  }
  {
    auto s=make(ScopeKind::TableHeader,"header",92);s.parent_ref="table-column-1";
    assert(ScopeSpanValidator::validate(s,source,nodes).effect==ScopeEffect::TableHeaderScope);
  }
  {
    auto s=make(ScopeKind::Negation,"not",0);s.target_refs={"c1","c2"};
    assert(ScopeSpanValidator::validate(s,source,nodes).error=="NEGATION_TARGET_INVALID");
    s.target_refs={"c1"};s.opener.exact_text="wrong";
    assert(ScopeSpanValidator::validate(s,source,nodes).error=="SCOPE_OPENER_ANCHOR_INVALID");
  }
  std::cout<<"SURGE ScopeSpan validator tests: 12 groups passed\n";
}
