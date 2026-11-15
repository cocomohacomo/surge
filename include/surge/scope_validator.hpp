#pragma once
#include "surge/context_graph.hpp"
namespace surge {
class ScopeSpanValidator {
 public:
  // Offsets are half-open UTF-8 byte offsets into normalized_source.
  static ScopeValidationResult validate(const ScopeSpanProposal&, const std::string& normalized_source,
                                        const std::map<std::string, GraphNode>& nodes);
};
}  // namespace surge
