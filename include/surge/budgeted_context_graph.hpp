#pragma once

#include "surge/context_graph.hpp"
#include "surge/resource_budget.hpp"
#include <cstddef>
#include <vector>
#include <functional>
#include <string>
#include <utility>

namespace surge {

struct ClusterRetentionPlan;

// Opt-in admission facade for ContextGraph::apply. All semantic-delta writes
// must pass through this facade for its limits to be effective. Direct calls
// to ContextGraph mutators remain outside this guard.
class BudgetedContextGraph {
 public:
  BudgetedContextGraph(ContextGraph& graph, ResourceBudget& budget,
                       std::size_t max_staging_bytes = 0);
  bool recover(std::string& error);
  DeltaResult apply(const SemanticDelta& delta);
  // Import a complete archive through the same transient/resident admission budget.
  DeltaResult restore_cluster_archive(const std::string& payload,
      std::uint64_t expected_state_version, const std::string& transaction_id,
      const std::string& idempotency_key);

  // Atomic Core-owned classification + budgeted persistence. Callers provide
  // hints and a base delta; cluster identity and membership are assigned by
  // ContextGraph, then committed through the same resource admission gate.
  DeltaResult apply_retention_plan(const ClusterRetentionPlan& plan,
      const std::string& recorded_at, const std::string& reason,
      const std::string& provenance_digest, const std::string& delta_id,
      const std::string& idempotency_key, std::string& error);

  DeltaResult append_cluster_lifecycle(SemanticDelta delta,
      const ClusterLifecycleRecord& record, const std::string& lifecycle_node_id,
      const std::string& relation_id, std::string& error);

  DeltaResult append_entity_reclamation(SemanticDelta delta,
      const std::string& entity_node_id, std::string& error);

  DeltaResult append_cluster_unmembership(SemanticDelta delta,
      const std::string& cluster_id, const std::string& member_node_id,
      std::string& error);

  DeltaResult append_unresolved_membership(SemanticDelta delta,
      const std::string& member_node_id, std::string& chaos_cluster_id,
      std::string& membership_relation_id, std::string& error);

  DeltaResult append_cluster_membership(SemanticDelta delta,
      const std::string& parent_cluster_id, const std::string& classification,
      const std::string& partition_key, const std::string& member_node_id,
      const std::string& relation_prefix, std::string& assigned_cluster_id,
      std::string& error);

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
  bool reserve_staging(std::size_t bytes, ResourceLease& staging,
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
