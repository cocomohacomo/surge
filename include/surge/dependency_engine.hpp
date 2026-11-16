#pragma once
#include "surge/context_graph.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace surge {
struct DependencyEvaluationResult { bool complete{false}; std::size_t predicate_operations{0}; std::vector<std::string> triggered_dependencies; std::vector<std::string> impacted_nodes; std::vector<std::string> rejected_dependencies; std::string error; };
struct ImpactResult {
  bool complete{false};
  std::vector<std::string> impacted_nodes;
  std::vector<std::vector<std::string>> cyclic_components;
  std::string error;
};
class DependencyEngine {
 public:
  // Dependency node references are [prerequisite, dependent].
  static DependencyEvaluationResult evaluate(const std::map<std::string, GraphNode>& nodes,\n      const std::vector<std::string>& changed_nodes, std::size_t maximum_nodes,\n      std::size_t maximum_predicate_operations = 65536);
  static ImpactResult compute(const std::map<std::string, GraphNode>& nodes,
                              const std::vector<std::string>& changed_nodes,
                              std::size_t maximum_nodes);
};
}  // namespace surge
