#pragma once

#include "surge/context_graph.hpp"
#include "surge/resource_budget.hpp"
#include <cstddef>
#include <vector>
#include <functional>
#include <string>
#include <utility>

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

  // Guarded wrappers for journal-backed Question/QuestionAspect mutations.
  bool record_question_reference(const std::string& id, std::uint64_t expected,
      bool& reactivated, std::string& error);
  bool record_question_reference_with_dependents(const std::string& id,
      std::uint64_t expected, std::size_t max_depth, std::size_t max_nodes,
      bool& reactivated, std::size_t& reactivated_dependents, std::string& error);
  bool set_question_memory_state(const std::string& id, QuestionMemoryState state,
      std::uint64_t expected, std::string& error);
  bool enforce_question_memory_capacity(
      const std::vector<std::pair<std::string, double>>& scores,
      std::size_t max_active, std::uint64_t expected,
      std::vector<std::string>& demoted, std::string& error);
  bool set_question_aspect_status(const std::string& id, QuestionAspectStatus status,
      std::uint64_t expected, std::string& error);
  bool apply_question_aspect_statuses(
      const std::vector<std::pair<std::string, QuestionAspectStatus>>& updates,
      std::uint64_t expected, std::string& error);
  bool reassess_claim(const std::string& claim_id, const std::string& reference_at,
      std::uint64_t expected, TemporalReassessmentRecord& result, std::string& error);
  std::size_t accounted_resident_bytes() const noexcept {
    return accounted_resident_bytes_;
  }

 private:
  static bool estimate_delta(const SemanticDelta& delta,
                             std::size_t& resident_bytes,
                             std::size_t& cluster_count,
                             std::size_t& retained_events,
                             std::string& error);
  static bool estimate_graph(const ContextGraph& graph,
                             std::size_t& resident_bytes,
                             std::size_t& cluster_count,
                             std::size_t& retained_events,
                             std::string& error);
  bool reserve_mutation(std::size_t bytes, std::size_t events,
      ResourceLease& resident, ResourceLease& retained, std::string& error);
  void publish_mutation(ResourceLease&& resident, ResourceLease&& retained);
  ContextGraph& graph_;
  ResourceBudget& budget_;
  std::size_t max_staging_bytes_{0};
  std::size_t accounted_resident_bytes_{0};
  bool recovered_{false};
  std::vector<ResourceLease> resident_leases_;
  std::vector<ResourceLease> cluster_leases_;
  std::vector<ResourceLease> recovery_event_leases_;
  std::vector<ResourceLease> mutation_resident_leases_;
  std::vector<ResourceLease> mutation_event_leases_;
};

}  // namespace surge
