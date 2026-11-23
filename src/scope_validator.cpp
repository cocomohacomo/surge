#include "surge/scope_validator.hpp"
#include "surge/sha256.hpp"

#include <algorithm>
#include <cctype>

namespace surge {
namespace {

bool valid_digest(const std::string& digest) {
  return digest.size() == 64 &&
      std::all_of(digest.begin(), digest.end(),
                   [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool anchor_matches_source(const ScopeAnchor& anchor,
                          const ScopeSpanProposal& span,
                          const std::string& source) {
  if (anchor.anchor_id.empty() || anchor.source_digest != span.source_digest ||
      anchor.offset_basis != span.offset_basis || anchor.packet_ref.empty() ||
      anchor.start_offset >= anchor.end_offset ||
      anchor.end_offset > source.size() || anchor.exact_text.empty()) {
    return false;
  }
  return source.compare(anchor.start_offset,
                       anchor.end_offset - anchor.start_offset,
                       anchor.exact_text) == 0;
}

bool packet_is_bound(const ScopeSpanProposal& span, const std::string& packet_ref) {
  return std::find(span.packet_refs.begin(), span.packet_refs.end(), packet_ref) !=
      span.packet_refs.end();
}

bool targets_have_kind(const ScopeSpanProposal& span,
                      const std::map<std::string, GraphNode>& nodes,
                      std::size_t expected_count, NodeKind expected_kind) {
  if (span.target_refs.size() != expected_count) return false;
  for (const auto& ref : span.target_refs) {
    const auto node = nodes.find(ref);
    if (node == nodes.end() || node->second.kind != expected_kind) return false;
  }
  return true;
}

bool all_targets_exist(const ScopeSpanProposal& span,
                      const std::map<std::string, GraphNode>& nodes) {
  return std::all_of(span.target_refs.begin(), span.target_refs.end(),
                     [&](const std::string& ref) { return nodes.count(ref) != 0; });
}

ScopeValidationResult reject(const char* error) {
  ScopeValidationResult result;
  result.error = error;
  return result;
}

ScopeValidationResult unresolved(ScopeEffect effect, const char* reason) {
  ScopeValidationResult result;
  result.accepted = true;
  result.status = ScopeStatus::Unresolved;
  result.effect = effect;
  result.error = reason;
  return result;
}

ScopeValidationResult closed(ScopeEffect effect, bool unconditional = false) {
  ScopeValidationResult result;
  result.accepted = true;
  result.status = ScopeStatus::Closed;
  result.effect = effect;
  result.may_emit_unconditional_claim = unconditional;
  return result;
}

}  // namespace

ScopeValidationResult ScopeSpanValidator::validate(
    const ScopeSpanProposal& span, const std::string& source,
    const std::map<std::string, GraphNode>& nodes) {
  if (span.span_id.empty() || !valid_digest(span.source_digest) ||
      span.offset_basis != "UTF8_BYTE_NFC" || span.packet_refs.empty()) {
    return reject("SCOPE_SOURCE_IDENTITY_INVALID");
  }
  if (!is_valid_utf8(source) || sha256_hex(source) != span.source_digest) {
    return reject("SCOPE_SOURCE_DIGEST_MISMATCH");
  }
  if (!anchor_matches_source(span.opener, span, source) ||
      !packet_is_bound(span, span.opener.packet_ref)) {
    return reject("SCOPE_OPENER_ANCHOR_INVALID");
  }
  if (span.closer) {
    const auto& closer = *span.closer;
    if (closer.anchor_id == span.opener.anchor_id ||
        !anchor_matches_source(closer, span, source) ||
        !packet_is_bound(span, closer.packet_ref) ||
        closer.start_offset < span.opener.end_offset) {
      return reject("SCOPE_CLOSER_ANCHOR_INVALID");
    }
  }
  if (!all_targets_exist(span, nodes)) return reject("SCOPE_TARGET_NOT_FOUND");

  switch (span.kind) {
    case ScopeKind::Negation:
      if (!targets_have_kind(span, nodes, 1, NodeKind::Claim))
        return reject("NEGATION_TARGET_INVALID");
      return closed(ScopeEffect::NegatedClaim);

    case ScopeKind::Condition:
      if (!targets_have_kind(span, nodes, 2, NodeKind::Claim))
        return reject("CONDITION_TARGETS_INVALID");
      if (!span.closer)
        return unresolved(ScopeEffect::ConditionalRelation, "CONDITION_SCOPE_OPEN");
      return closed(ScopeEffect::ConditionalRelation);

    case ScopeKind::Modality:
      if (!targets_have_kind(span, nodes, 1, NodeKind::Claim) ||
          span.modality_kind.empty())
        return reject("MODALITY_TARGET_OR_KIND_INVALID");
      return closed(ScopeEffect::ModalityQualified);

    case ScopeKind::Concession:
      if (!targets_have_kind(span, nodes, 2, NodeKind::Claim))
        return reject("CONCESSION_TARGETS_INVALID");
      if (!span.closer)
        return unresolved(ScopeEffect::ConcessionRelation, "CONCESSION_SCOPE_OPEN");
      return closed(ScopeEffect::ConcessionRelation);

    case ScopeKind::Contrast:
      if (!targets_have_kind(span, nodes, 2, NodeKind::Claim))
        return reject("CONTRAST_TARGETS_INVALID");
      if (!span.closer)
        return unresolved(ScopeEffect::ContrastRelation, "CONTRAST_SCOPE_OPEN");
      return closed(ScopeEffect::ContrastRelation);

    case ScopeKind::Coreference:
    case ScopeKind::Anaphora:
      if (span.target_refs.size() != 1 || span.antecedent_refs.empty())
        return reject("REFERENCE_SCOPE_INVALID");
      for (const auto& ref : span.antecedent_refs) {
        const auto node = nodes.find(ref);
        if (node == nodes.end() || node->second.kind != NodeKind::Entity)
          return reject("ANTECEDENT_CANDIDATE_INVALID");
      }
      if (span.antecedent_refs.size() != 1)
        return unresolved(ScopeEffect::ReferenceCandidate, "ANTECEDENT_AMBIGUOUS");
      return closed(ScopeEffect::ReferenceCandidate);

    case ScopeKind::Quotation:
      if (!span.closer)
        return unresolved(ScopeEffect::QuotationEvidence, "QUOTATION_SCOPE_OPEN");
      if (!span.attribution_ref.empty()) {
        const auto attribution = nodes.find(span.attribution_ref);
        if (attribution == nodes.end() || attribution->second.kind != NodeKind::Entity)
          return reject("QUOTE_ATTRIBUTION_INVALID");
      }
      return closed(ScopeEffect::QuotationEvidence);

    case ScopeKind::Temporal:
      if (!targets_have_kind(span, nodes, 1, NodeKind::Claim) ||
          span.temporal_role.empty())
        return reject("TEMPORAL_TARGET_OR_ROLE_INVALID");
      return closed(ScopeEffect::TemporalQualifier);

    case ScopeKind::ListContinuation:
      if (span.parent_ref.empty()) return reject("LIST_PARENT_REF_MISSING");
      return closed(ScopeEffect::StructuralContinuation);

    case ScopeKind::TableHeader:
      if (span.parent_ref.empty()) return reject("TABLE_HEADER_REF_MISSING");
      return closed(ScopeEffect::TableHeaderScope);
  }
  return reject("SCOPE_KIND_UNKNOWN");
}
}  // namespace surge
