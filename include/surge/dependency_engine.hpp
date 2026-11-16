#pragma once
#include "surge/context_graph.hpp"
#include <cstddef>
#include <string>
#include <optional>
#include <vector>

namespace surge {
enum class DependencyPredicateKind { OnChange, Present, ContentEquals, ContentContains, Kind, Invalid };
struct DependencyPredicate {
  DependencyPredicateKind kind{DependencyPredicateKind::Invalid};
  std::string operand;
  std::string expected_node_kind;
  std::string error;
  bool valid() const { return kind != DependencyPredicateKind::Invalid; }
};
struct DependencyEvaluationResult { bool complete{false}; std::vector<std::string> triggered_dependencies; std::vector<std::string> impacted_nodes; std::vector<std::string> rejected_dependencies; std::string error; };
enum class FixpointStatus { Converged, Deferred, Invalid };
struct FixpointEvaluationResult {
  FixpointStatus status{FixpointStatus::Invalid};
  std::size_t iterations{0};
  std::size_t evaluation_cost{0};
  std::vector<std::string> triggered_dependencies;
  std::vector<std::string> impacted_nodes;
  std::vector<std::string> rejected_dependencies;
  std::vector<std::vector<std::string>> cyclic_components;
  std::string error;
};
struct ImpactResult {
  bool complete{false};
  std::vector<std::string> impacted_nodes;
  std::vector<std::vector<std::string>> cyclic_components;
  std::string error;
};
class DependencyEngine {
 public:
  // Dependency node references are [prerequisite, dependent].
  static DependencyPredicate parse_predicate(const std::string& expression);
  static DependencyEvaluationResult evaluate(const std::map<std::string, GraphNode>& nodes, const std::vector<std::string>& changed_nodes, std::size_t maximum_nodes);
  static FixpointEvaluationResult evaluate_fixpoint(
      const std::map<std::string, GraphNode>& nodes,
      const std::vector<std::string>& changed_nodes,
      std::size_t maximum_nodes,
      std::size_t maximum_iterations,
      std::size_t maximum_evaluations);
  static ImpactResult compute(const std::map<std::string, GraphNode>& nodes,
                              const std::vector<std::string>& changed_nodes,
                              std::size_t maximum_nodes);
};
}  // namespace surge
