#pragma once
#include "surge/context_graph.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace surge {
struct ImpactResult {
  bool complete{false};
  std::vector<std::string> impacted_nodes;
  std::vector<std::vector<std::string>> cyclic_components;
  std::string error;
};
class DependencyEngine {
 public:
  // Dependency node references are [prerequisite, dependent].
  static ImpactResult compute(const std::map<std::string, GraphNode>& nodes,
                              const std::vector<std::string>& changed_nodes,
                              std::size_t maximum_nodes);
};
}  // namespace surge
