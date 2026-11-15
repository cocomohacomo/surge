#pragma once
#include "surge/context_graph.hpp"
namespace surge {
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
  static ScopeValidationResult validate(const ScopeSpanProposal&, const std::string& normalized_source,
                                        const std::map<std::string, GraphNode>& nodes);
};
}  // namespace surge
