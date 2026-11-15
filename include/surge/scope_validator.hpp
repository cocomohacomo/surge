#pragma once
#include "surge/context_graph.hpp"
#include <optional>
#include <string>
#include <vector>

namespace surge {
enum class ScopeKind {
  Negation, Condition, Modality, Concession, Contrast, Coreference,
  Anaphora, Quotation, Temporal, ListContinuation, TableHeader
};
enum class ScopeStatus { Closed, Unresolved };
enum class ScopeEffect {
  NegatedClaim, ConditionalRelation, ModalityQualified, ConcessionRelation,
  ContrastRelation, ReferenceCandidate, QuotationEvidence, TemporalQualifier,
  StructuralContinuation, TableHeaderScope
};
struct ScopeAnchor {
  std::string anchor_id;
  std::string source_digest;
  std::string offset_basis;
  std::size_t start_offset{0};
  std::size_t end_offset{0};
  std::string exact_text;
  std::string packet_ref;
};
struct ScopeSpanProposal {
  std::string span_id;
  ScopeKind kind{ScopeKind::Negation};
  std::string source_digest;
  std::string offset_basis;
  ScopeAnchor opener;
  std::optional<ScopeAnchor> closer;
  std::vector<std::string> target_refs;
  std::vector<std::string> antecedent_refs;
  std::string attribution_ref; // empty means attribution is explicitly UNKNOWN
  std::string modality_kind;
  std::string temporal_role;
  std::string parent_ref;
  std::vector<std::string> packet_refs;
};
struct ScopeValidationResult {
  bool accepted{false};
  ScopeStatus status{ScopeStatus::Unresolved};
  ScopeEffect effect{ScopeEffect::ReferenceCandidate};
  bool may_emit_unconditional_claim{false};
  std::string error;
};
class ScopeSpanValidator {
 public:
  // Offsets are half-open UTF-8 byte offsets into normalized_source.
  // Anchors must match exact source bytes and the span's immutable source digest.
  static ScopeValidationResult validate(
      const ScopeSpanProposal& span, const std::string& normalized_source,
      const std::map<std::string, GraphNode>& nodes);
};
}  // namespace surge
