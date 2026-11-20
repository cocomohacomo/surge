#include "surge/cluster_archive_store.hpp"
#include "surge/budgeted_context_graph.hpp"
#include <utility>
#include <set>
#include <algorithm>
#include <sstream>
#include <iomanip>
namespace surge {
namespace { constexpr const char* kSchema = "surge.cluster.archive.v1"; }
namespace {
bool append_archive_string(std::ostringstream& out, const std::string& value) {
  out << std::quoted(value);
  return static_cast<bool>(out);
}
}

bool serialize_cluster_archive_payload(const ContextGraph& graph,
    const std::string& cluster_id, std::string* payload, std::string* error) {
  if (!payload || cluster_id.empty()) {
    if (error) *error = "CLUSTER_ARCHIVE_SERIALIZE_ARGUMENT_INVALID";
    return false;
  }
  const auto root = graph.nodes().find(cluster_id);
  if (root == graph.nodes().end() ||
      (root->second.kind != NodeKind::Cluster &&
       root->second.kind != NodeKind::ChaosCluster)) {
    if (error) *error = "CLUSTER_ARCHIVE_SERIALIZE_CLUSTER_NOT_FOUND";
    return false;
  }

  // Deterministic containment closure. Cluster children are traversed recursively;
  // typed references are included as canonical records, without following arbitrary
  // semantic relations that could pull unrelated topics into this archive.
  std::set<std::string> included{cluster_id};
  std::vector<std::string> pending{cluster_id};
  while (!pending.empty()) {
    const auto parent = pending.back();
    pending.pop_back();
    for (const auto& entry : graph.relations()) {
      const auto& edge = entry.second;
      if (edge.kind != RelationKind::Contains || edge.from != parent) continue;
      const auto node = graph.nodes().find(edge.to);
      if (node == graph.nodes().end()) continue;
      if (included.insert(edge.to).second &&
          (node->second.kind == NodeKind::Cluster ||
           node->second.kind == NodeKind::ChaosCluster))
        pending.push_back(edge.to);
    }
  }
  // Include referenced records so the archive does not preserve dangling typed IDs.
  std::vector<std::string> frontier(included.begin(), included.end());
  for (const auto& id : frontier) {
    const auto node = graph.nodes().find(id);
    if (node == graph.nodes().end()) continue;
    for (const auto& ref : node->second.references)
      if (graph.nodes().count(ref)) included.insert(ref);
  }

  std::ostringstream out;
  out << "SURGE_CLUSTER_ARCHIVE_PAYLOAD 1\n";
  out << "watermark " << graph.state_version() << "\n";
  out << "cluster " << std::quoted(cluster_id) << "\n";
  std::size_t node_count = 0;
  for (const auto& id : included) if (graph.nodes().count(id)) ++node_count;
  out << "nodes " << node_count << "\n";
  for (const auto& id : included) {
    const auto it = graph.nodes().find(id);
    if (it == graph.nodes().end()) continue;
    const auto& node = it->second;
    out << "node " << std::quoted(node.id) << ' '
        << static_cast<unsigned>(node.kind) << ' '
        << std::quoted(node.content) << ' ' << node.references.size();
    for (const auto& ref : node.references) out << ' ' << std::quoted(ref);
    out << "\n";
  }
  std::size_t relation_count = 0;
  for (const auto& entry : graph.relations())
    if (included.count(entry.second.from) && included.count(entry.second.to))
      ++relation_count;
  out << "relations " << relation_count << "\n";
  for (const auto& entry : graph.relations()) {
    const auto& edge = entry.second;
    if (!included.count(edge.from) || !included.count(edge.to)) continue;
    out << "relation " << std::quoted(edge.id) << ' '
        << static_cast<unsigned>(edge.kind) << ' '
        << std::quoted(edge.from) << ' ' << std::quoted(edge.to) << "\n";
  }
  const auto write_focus = [&](const FocusRecord& focus) {
    out << "focus " << std::quoted(focus.cluster_id) << ' '
        << std::quoted(focus.interest_id) << ' ' << (focus.chaos_cluster ? 1 : 0) << "\n";
  };
  if (graph.focus() && included.count(graph.focus()->cluster_id)) {
    out << "has_focus 1\n";
    write_focus(*graph.focus());
  } else out << "has_focus 0\n";
  std::size_t history_count = 0;
  for (const auto& focus : graph.focus_history())
    if (included.count(focus.cluster_id)) ++history_count;
  out << "focus_history " << history_count << "\n";
  for (const auto& focus : graph.focus_history())
    if (included.count(focus.cluster_id)) write_focus(focus);
  if (!out) {
    if (error) *error = "CLUSTER_ARCHIVE_SERIALIZE_FAILED";
    return false;
  }
  *payload = out.str();
  if (error) error->clear();
  return true;
}

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
    const std::vector<std::string>& member_node_ids,
    std::uint64_t expected_state_version, const std::string& delta_id,
    const std::string& idempotency_key) {
  std::string payload;
  std::string error;
  if (!serialize_cluster_archive_payload(graph, cluster_id, &payload, &error)) {
    ClusterArchiveDetachResult result;
    result.error = error;
    return result;
  }
  return archive_and_detach_cluster(graph, budgeted_graph, archive_store,
      cluster_id, member_node_ids, payload, expected_state_version,
      delta_id, idempotency_key);
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
