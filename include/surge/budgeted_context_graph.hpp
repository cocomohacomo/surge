#pragma once

#include "surge/context_graph.hpp"
#include "surge/resource_budget.hpp"
#include <cstddef>
#include <vector>

namespace surge {

// Opt-in admission facade for ContextGraph::apply. All semantic-delta writes
// must pass through this facade for its limits to be effective. Direct calls
// to ContextGraph mutators remain outside this guard.
class BudgetedContextGraph {
 public:
  BudgetedContextGraph(ContextGraph& graph, ResourceBudget& budget,
                       std::size_t max_staging_bytes = 0);
  bool recover(std::string& error);
  DeltaResult apply(const SemanticDelta& delta);
  std::size_t accounted_resident_bytes() const noexcept {
    return accounted_resident_bytes_;
  }

 private:
  static bool estimate_delta(const SemanticDelta& delta,
                             std::size_t& resident_bytes,
                             std::size_t& cluster_count,
                             std::string& error);
  static bool estimate_graph(const ContextGraph& graph,
                             std::size_t& resident_bytes,
                             std::size_t& cluster_count,
                             std::string& error);
  ContextGraph& graph_;
  ResourceBudget& budget_;
  std::size_t max_staging_bytes_{0};
  std::size_t accounted_resident_bytes_{0};
  std::vector<ResourceLease> resident_leases_;
  std::vector<ResourceLease> cluster_leases_;
};

}  // namespace surge
