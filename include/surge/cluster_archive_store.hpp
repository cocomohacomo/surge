#pragma once
#include "surge/snapshot_store.hpp"
#include "surge/context_graph.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
namespace surge {
class BudgetedContextGraph;
struct ClusterArchiveRecord {
  std::string cluster_id;
  std::uint64_t state_version{0};
  std::uint64_t journal_sequence{0};
  std::string payload;
};
struct ClusterArchiveDetachPlan {
  std::string cluster_id;
  std::vector<std::string> exclusively_owned_entity_ids;
  std::vector<std::string> shared_member_ids;
  std::vector<std::string> ineligible_member_ids;
};
// Produces a deterministic, versioned snapshot of the cluster's containment closure.
// The payload includes graph nodes, typed references, internal relations and Focus metadata.
bool serialize_cluster_archive_payload(const ContextGraph& graph,
    const std::string& cluster_id, std::string* payload, std::string* error);

// Validates the v2 payload identity preamble before it can authorize graph detachment.
// This is deliberately a preflight guard, not a complete payload decoder.
bool validate_cluster_archive_payload_identity(const std::string& payload,
    const std::string& expected_cluster_id, std::uint64_t expected_state_version,
    std::string* error);

// Computes a conservative detach plan. It never mutates the graph.
ClusterArchiveDetachPlan plan_cluster_archive_detach(
    const ContextGraph& graph, const std::string& cluster_id,
    const std::vector<std::string>& member_node_ids);

struct ClusterArchiveDetachResult {
  bool archive_verified{false};
  bool graph_detached{false};
  std::uint64_t archived_state_version{0};
  std::uint64_t committed_state_version{0};
  std::vector<std::string> reclaimed_entity_ids;
  std::vector<std::string> preserved_shared_member_ids;
  std::vector<std::string> ineligible_member_ids;
  std::string error;
};

// Serializes the graph itself, archives the resulting bytes at the exact graph watermark,
// verifies readback, then detaches eligible memberships in one journal delta.
ClusterArchiveDetachResult archive_and_detach_cluster(
    const ContextGraph& graph, BudgetedContextGraph& budgeted_graph,
    class ClusterArchiveStore& archive_store, const std::string& cluster_id,
    const std::vector<std::string>& member_node_ids,
    std::uint64_t expected_state_version, const std::string& delta_id,
    const std::string& idempotency_key);

// Lower-level overload for callers that already hold a canonical payload.
ClusterArchiveDetachResult archive_and_detach_cluster(
    const ContextGraph& graph, BudgetedContextGraph& budgeted_graph,
    class ClusterArchiveStore& archive_store, const std::string& cluster_id,
    const std::vector<std::string>& member_node_ids, const std::string& payload,
    std::uint64_t expected_state_version, const std::string& delta_id,
    const std::string& idempotency_key);

// Durable per-cluster payload archive. SnapshotStore supplies atomic
// generations, SHA-256 validation, and backup recovery. This component
// deliberately does not mutate ContextGraph.
class ClusterArchiveStore {
 public:
  // Cluster archives may exceed the generic snapshot default; the 256 MiB ceiling
  // is archive-specific and can be overridden by callers.
  explicit ClusterArchiveStore(std::string path,
      SnapshotStoreLimits limits = SnapshotStoreLimits{256U * 1024U * 1024U, true});
  bool archive(const std::string& cluster_id, std::uint64_t state_version,
      std::uint64_t journal_sequence, const std::string& payload,
      std::string* error);
  bool restore(const std::string& expected_cluster_id,
      std::uint64_t minimum_state_version, ClusterArchiveRecord* output,
      std::string* error, std::uint64_t minimum_journal_sequence = 0) const;
  // Persists an archive and reads it back, requiring exact identity, watermarks,
  // and payload equality before reporting success.
  bool archive_verified(const std::string& cluster_id,
      std::uint64_t state_version, std::uint64_t journal_sequence,
      const std::string& payload, ClusterArchiveRecord* verified,
      std::string* error);
  const std::string& path() const noexcept { return store_.path(); }
 private:
  SnapshotStore store_;
};
}  // namespace surge
