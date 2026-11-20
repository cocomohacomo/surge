#include "surge/cluster_archive_store.hpp"
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
      if (relation.kind == RelationKind::Contains &&
          relation.from == cluster_id && relation.to == id) continue;
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
}  // namespace surge
