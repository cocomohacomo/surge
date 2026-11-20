#include "surge/cluster_archive_store.hpp"
#include "surge/budgeted_context_graph.hpp"
#include <utility>
#include <set>
#include <algorithm>
namespace surge {
namespace { constexpr const char* kSchema = "surge.cluster.archive.v1"; }
ClusterArchiveDetachPlan plan_cluster_archive_detach(
    const ContextGraph& graph, const std::string& cluster_id,
    const std::vector<std::string>& member_node_ids) {
  ClusterArchiveDetachPlan plan;
  plan.cluster_id = cluster_id;
  const auto cluster = graph.nodes().find(cluster_id);
  if (cluster == graph.nodes().end() ||
      (cluster->second.kind != NodeKind::Cluster &&
       cluster->second.kind != NodeKind::ChaosCluster)) {
    plan.ineligible_member_ids = member_node_ids;
    return plan;
  }
  std::set<std::string> seen;
  for (const auto& id : member_node_ids) {
    if (!seen.insert(id).second) continue;
    const auto node = graph.nodes().find(id);
    if (node == graph.nodes().end() || node->second.kind != NodeKind::Entity) {
      plan.ineligible_member_ids.push_back(id);
      continue;
    }
    bool member_of_target = false;
    for (const auto& edge : graph.relations()) {
      const auto& relation = edge.second;
      if (relation.kind == RelationKind::Contains &&
          relation.from == cluster_id && relation.to == id) {
        member_of_target = true;
        break;
      }
    }
    if (!member_of_target) {
      plan.ineligible_member_ids.push_back(id);
      continue;
    }
    bool has_other_relation = false;
    for (const auto& edge : graph.relations()) {
      const auto& relation = edge.second;
      if (relation.kind == RelationKind::Contains && relation.to == id) {
        const auto parent = graph.nodes().find(relation.from);
        if (parent != graph.nodes().end() &&
            (parent->second.kind == NodeKind::Cluster ||
             parent->second.kind == NodeKind::ChaosCluster)) continue;
      }
      if (relation.from == id || relation.to == id) {
        has_other_relation = true;
        break;
      }
    }
    bool has_typed_reference = false;
    for (const auto& other : graph.nodes()) {
      if (other.first == id) continue;
      if (std::find(other.second.references.begin(), other.second.references.end(), id)
          != other.second.references.end()) {
        has_typed_reference = true;
        break;
      }
    }
    if (has_other_relation || has_typed_reference) {
      plan.ineligible_member_ids.push_back(id);
      continue;
    }
    if (graph.cluster_membership_reference_count(id) == 1)
      plan.exclusively_owned_entity_ids.push_back(id);
    else
      plan.shared_member_ids.push_back(id);
  }
  return plan;
}
ClusterArchiveStore::ClusterArchiveStore(std::string path, SnapshotStoreLimits limits)
    : store_(std::move(path), limits) {}
bool ClusterArchiveStore::archive(const std::string& cluster_id,
    std::uint64_t state_version, std::uint64_t journal_sequence,
    const std::string& payload, std::string* error) {
  if (cluster_id.empty() || state_version == 0 || payload.empty()) {
    if (error) *error = "CLUSTER_ARCHIVE_INPUT_INVALID";
    return false;
  }
  SnapshotDocument document;
  document.schema_id = kSchema; document.context_id = cluster_id;
  document.state_version = state_version; document.journal_sequence = journal_sequence;
  document.payload = payload;
  std::string bytes;
  if (!SnapshotCodec::encode(document, {}, &bytes, error)) return false;
  return store_.save(bytes, error);
}
bool ClusterArchiveStore::restore(const std::string& expected_cluster_id,
    std::uint64_t minimum_state_version, ClusterArchiveRecord* output,
    std::string* error, std::uint64_t minimum_journal_sequence) const {
  if (!output || expected_cluster_id.empty()) {
    if (error) *error = "CLUSTER_ARCHIVE_RESTORE_ARGUMENT_INVALID";
    return false;
  }
  auto loaded = store_.load();
  if (!loaded.ok) {
    if (error) *error = loaded.error.empty() ? "CLUSTER_ARCHIVE_GENERATION_UNAVAILABLE" : loaded.error;
    return false;
  }
  if (loaded.document.schema_id != kSchema ||
      loaded.document.context_id != expected_cluster_id ||
      loaded.document.state_version < minimum_state_version ||
      loaded.document.journal_sequence < minimum_journal_sequence) {
    if (error) *error = "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH";
    return false;
  }
  SnapshotDocument decoded;
  if (!SnapshotCodec::decode(loaded.bytes, {}, &decoded, error)) return false;
  if (decoded.schema_id != kSchema || decoded.context_id != expected_cluster_id ||
      decoded.state_version < minimum_state_version ||
      decoded.journal_sequence < minimum_journal_sequence) {
    if (error) *error = "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH";
    return false;
  }
  ClusterArchiveRecord candidate;
  candidate.cluster_id = decoded.context_id; candidate.state_version = decoded.state_version;
  candidate.journal_sequence = decoded.journal_sequence; candidate.payload = std::move(decoded.payload);
  *output = std::move(candidate);
  if (error) error->clear();
  return true;
}

bool ClusterArchiveStore::archive_verified(const std::string& cluster_id,
    std::uint64_t state_version, std::uint64_t journal_sequence,
    const std::string& payload, ClusterArchiveRecord* verified,
    std::string* error) {
  if (!verified) {
    if (error) *error = "CLUSTER_ARCHIVE_VERIFY_OUTPUT_INVALID";
    return false;
  }
  if (!archive(cluster_id, state_version, journal_sequence, payload, error))
    return false;
  ClusterArchiveRecord candidate;
  if (!restore(cluster_id, state_version, &candidate, error, journal_sequence))
    return false;
  if (candidate.state_version != state_version ||
      candidate.journal_sequence != journal_sequence ||
      candidate.payload != payload) {
    if (error) *error = "CLUSTER_ARCHIVE_VERIFY_MISMATCH";
    return false;
  }
  *verified = std::move(candidate);
  if (error) error->clear();
  return true;
}

ClusterArchiveDetachResult archive_and_detach_cluster(
    const ContextGraph& graph, BudgetedContextGraph& budgeted_graph,
    ClusterArchiveStore& archive_store, const std::string& cluster_id,
    const std::vector<std::string>& member_node_ids, const std::string& payload,
    std::uint64_t expected_state_version, const std::string& delta_id,
    const std::string& idempotency_key) {
  ClusterArchiveDetachResult result;
  auto fail = [&](const std::string& error) {
    result.error = error;
    return result;
  };
  if (cluster_id.empty() || payload.empty() || delta_id.empty() ||
      idempotency_key.empty() || expected_state_version == 0)
    return fail("CLUSTER_ARCHIVE_DETACH_ARGUMENT_INVALID");
  if (graph.state_version() != expected_state_version)
    return fail("CLUSTER_ARCHIVE_DETACH_STALE_GRAPH");
  const auto plan = plan_cluster_archive_detach(graph, cluster_id, member_node_ids);
  result.preserved_shared_member_ids = plan.shared_member_ids;
  result.ineligible_member_ids = plan.ineligible_member_ids;
  if (plan.exclusively_owned_entity_ids.empty() &&
      plan.shared_member_ids.empty())
    return fail("CLUSTER_ARCHIVE_DETACH_NO_ELIGIBLE_MEMBERS");

  ClusterArchiveRecord verified;
  if (!archive_store.archive_verified(cluster_id, expected_state_version,
      expected_state_version, payload, &verified, &result.error))
    return result;
  result.archive_verified = true;
  result.archived_state_version = verified.state_version;

  // A concurrent graph commit after capture invalidates the archive as a
  // detach source. Leave the verified archive inert and preserve the graph.
  if (graph.state_version() != expected_state_version)
    return fail("CLUSTER_ARCHIVE_DETACH_STALE_AFTER_ARCHIVE");

  SemanticDelta delta;
  delta.delta_id = delta_id;
  delta.idempotency_key = idempotency_key;
  delta.expected_state_version = expected_state_version;
  for (const auto& id : plan.exclusively_owned_entity_ids) {
    if (!graph.append_cluster_unmembership(delta, cluster_id, id, result.error))
      return result;
  }
  for (const auto& id : plan.shared_member_ids) {
    if (!graph.append_cluster_unmembership(delta, cluster_id, id, result.error))
      return result;
  }
  for (const auto& id : plan.exclusively_owned_entity_ids) {
    if (!graph.append_entity_reclamation(delta, id, result.error))
      return result;
  }
  const auto committed = budgeted_graph.apply(delta);
  if (!committed.committed || committed.duplicate)
    return fail(committed.error.empty() ? "CLUSTER_ARCHIVE_DETACH_COMMIT_REJECTED"
                                        : committed.error);
  result.graph_detached = true;
  result.committed_state_version = committed.state_version;
  result.reclaimed_entity_ids = plan.exclusively_owned_entity_ids;
  result.error.clear();
  return result;
}
}  // namespace surge
