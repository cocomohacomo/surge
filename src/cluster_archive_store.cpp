#include "surge/cluster_archive_store.hpp"
#include "surge/budgeted_context_graph.hpp"
#include <utility>
#include <set>
#include <algorithm>
#include <sstream>
#include <iomanip>
namespace surge {
namespace { constexpr const char* kSchema = "surge.cluster.archive.v2"; }
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
  out << "SURGE_CLUSTER_ARCHIVE_PAYLOAD 2\n";
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

  // Full ContextGraph history domains. Records are retained when their owning
  // node belongs to this archive; references remain explicit even if external.
  auto write_string_vector = [&out](const std::vector<std::string>& values) {
    out << values.size();
    for (const auto& value : values) out << ' ' << std::quoted(value);
    out << "\n";
  };
  std::size_t scope_count = 0;
  for (const auto& item : graph.scope_spans()) {
    const auto& p = item.second.proposal;
    const bool attached = included.count(p.span_id) ||
        included.count(p.opener.packet_ref) ||
        included.count(p.attribution_ref) || included.count(p.parent_ref) ||
        (p.closer && included.count(p.closer->packet_ref)) ||
        std::any_of(p.packet_refs.begin(), p.packet_refs.end(),
            [&](const std::string& id) { return included.count(id) != 0; }) ||
        std::any_of(p.target_refs.begin(), p.target_refs.end(),
            [&](const std::string& id) { return included.count(id) != 0; }) ||
        std::any_of(p.antecedent_refs.begin(), p.antecedent_refs.end(),
            [&](const std::string& id) { return included.count(id) != 0; });
    if (attached) ++scope_count;
  }
  out << "scope_spans " << scope_count << "\n";
  for (const auto& item : graph.scope_spans()) {
    const auto& r = item.second;
    const auto& p = r.proposal;
    const bool attached = included.count(p.span_id) ||
        included.count(p.opener.packet_ref) ||
        included.count(p.attribution_ref) || included.count(p.parent_ref) ||
        (p.closer && included.count(p.closer->packet_ref)) ||
        std::any_of(p.packet_refs.begin(), p.packet_refs.end(),
            [&](const std::string& id) { return included.count(id) != 0; }) ||
        std::any_of(p.target_refs.begin(), p.target_refs.end(),
            [&](const std::string& id) { return included.count(id) != 0; }) ||
        std::any_of(p.antecedent_refs.begin(), p.antecedent_refs.end(),
            [&](const std::string& id) { return included.count(id) != 0; });
    if (!attached) continue;
    auto write_anchor = [&out](const ScopeAnchor& a) {
      out << ' ' << std::quoted(a.anchor_id) << ' ' << std::quoted(a.source_digest)
          << ' ' << std::quoted(a.offset_basis) << ' ' << a.start_offset << ' '
          << a.end_offset << ' ' << std::quoted(a.exact_text) << ' '
          << std::quoted(a.packet_ref);
    };
    out << "scope " << std::quoted(item.first) << ' '
        << static_cast<unsigned>(p.kind) << ' ' << std::quoted(p.source_digest)
        << ' ' << std::quoted(p.offset_basis);
    write_anchor(p.opener);
    out << ' ' << (p.closer ? 1 : 0);
    if (p.closer) write_anchor(*p.closer);
    out << ' ';
    write_string_vector(p.target_refs);
    out << "scope_antecedents "; write_string_vector(p.antecedent_refs);
    out << "scope_meta " << std::quoted(p.attribution_ref) << ' '
        << std::quoted(p.modality_kind) << ' ' << std::quoted(p.temporal_role)
        << ' ' << std::quoted(p.parent_ref) << ' ';
    write_string_vector(p.packet_refs);
    out << "scope_result " << static_cast<unsigned>(r.status) << ' '
        << static_cast<unsigned>(r.effect) << ' '
        << (r.may_emit_unconditional_claim ? 1 : 0) << "\n";
  }

  std::size_t claim_history_count = 0;
  for (const auto& item : graph.claim_validity_history())
    if (included.count(item.first)) ++claim_history_count;
  out << "claim_validity_histories " << claim_history_count << "\n";
  for (const auto& item : graph.claim_validity_history()) {
    if (!included.count(item.first)) continue;
    out << "claim_history " << std::quoted(item.first) << ' ' << item.second.size() << "\n";
    for (const auto& record : item.second) {
      const auto& v = record.validity;
      out << "claim_validity " << std::quoted(v.claim_id) << ' '
          << std::quoted(v.valid_from) << ' ' << std::quoted(v.valid_until) << ' '
          << std::quoted(v.basis_evidence_id) << ' ' << v.revision << ' '
          << std::quoted(v.review_evidence_id) << ' ' << std::quoted(v.reviewer_id)
          << ' ' << record.recorded_state_version << "\n";
    }
  }
  std::size_t evidence_history_count = 0;
  for (const auto& item : graph.evidence_validity_history())
    if (included.count(item.first)) ++evidence_history_count;
  out << "evidence_validity_histories " << evidence_history_count << "\n";
  for (const auto& item : graph.evidence_validity_history()) {
    if (!included.count(item.first)) continue;
    out << "evidence_history " << std::quoted(item.first) << ' ' << item.second.size() << "\n";
    for (const auto& record : item.second) {
      const auto& v = record.validity;
      out << "evidence_validity " << std::quoted(v.evidence_id) << ' '
          << std::quoted(v.valid_from) << ' ' << std::quoted(v.valid_until) << ' '
          << std::quoted(v.basis_evidence_id) << ' ' << v.revision << ' '
          << std::quoted(v.review_evidence_id) << ' ' << std::quoted(v.reviewer_id)
          << ' ' << record.recorded_state_version << "\n";
    }
  }

  auto dependency_attached = [&](const DependencyEvaluationRecord& d) {
    if (std::any_of(d.impacted_nodes.begin(), d.impacted_nodes.end(),
        [&](const std::string& id) { return included.count(id) != 0; })) return true;
    return std::any_of(d.triggered.begin(), d.triggered.end(),
        [&](const DependencyTrigger& trigger) {
          return included.count(trigger.dependency_id) ||
                 included.count(trigger.prerequisite_id) ||
                 included.count(trigger.dependent_id);
        });
  };
  std::size_t dependency_count = 0;
  for (const auto& item : graph.dependency_evaluations())
    if (dependency_attached(item.second)) ++dependency_count;
  out << "dependency_evaluations " << dependency_count << "\n";
  for (const auto& item : graph.dependency_evaluations()) {
    if (!dependency_attached(item.second)) continue;
    const auto& d = item.second;
    out << "dependency " << std::quoted(item.first) << ' '
        << std::quoted(d.delta_id) << ' ' << d.triggered.size() << "\n";
    for (const auto& trigger : d.triggered)
      out << "trigger " << std::quoted(trigger.dependency_id) << ' '
          << std::quoted(trigger.prerequisite_id) << ' '
          << std::quoted(trigger.dependent_id) << "\n";
    out << "impacted "; write_string_vector(d.impacted_nodes);
    out << "rejected "; write_string_vector(d.rejected_dependencies);
  }

  std::size_t reassessment_count = 0;
  for (const auto& item : graph.reassessment_history())
    if (included.count(item.first)) ++reassessment_count;
  out << "reassessment_histories " << reassessment_count << "\n";
  for (const auto& item : graph.reassessment_history()) {
    if (!included.count(item.first)) continue;
    out << "reassessment " << std::quoted(item.first) << ' '
        << item.second.size() << "\n";
    for (const auto& r : item.second) {
      out << "reassessment_record " << std::quoted(r.trace_id) << ' '
          << std::quoted(r.claim_id) << ' ' << std::quoted(r.reference_at) << ' '
          << std::quoted(r.decision) << ' ' << std::quoted(r.claim_valid_from) << ' '
          << std::quoted(r.claim_valid_until) << ' '
          << std::quoted(r.claim_validity_basis_evidence_id) << ' '
          << std::quoted(r.reason_code) << ' ' << r.claim_validity_revision << ' '
          << r.claim_validity_recorded_state_version << ' '
          << r.evaluated_state_version << ' ' << r.recorded_state_version << ' '
          << r.sequence << ' ' << static_cast<unsigned>(r.lifecycle) << ' '
          << static_cast<unsigned>(r.claim_temporal_state) << ' '
          << std::quoted(r.previous_hash) << ' ' << std::quoted(r.snapshot_digest)
          << ' ' << std::quoted(r.chain_hash) << ' ' << r.evidence.size() << "\n";
      for (const auto& e : r.evidence)
        out << "reassessment_evidence " << std::quoted(e.evidence_id) << ' '
            << std::quoted(e.relation_id) << ' ' << static_cast<unsigned>(e.relation)
            << ' ' << e.validity_revision << ' ' << e.validity_recorded_state_version
            << ' ' << std::quoted(e.valid_from) << ' ' << std::quoted(e.valid_until)
            << ' ' << std::quoted(e.basis_evidence_id) << ' '
            << static_cast<unsigned>(e.temporal_state) << ' '
            << (e.eligible ? 1 : 0) << "\n";
      out << "active_supports "; write_string_vector(r.active_support_ids);
      out << "active_contradictions "; write_string_vector(r.active_contradiction_ids);
    }
  }

  out << "question_states";
  std::size_t question_state_count = 0;
  for (const auto& item : graph.nodes())
    if (item.second.kind == NodeKind::Question && included.count(item.first)) ++question_state_count;
  out << ' ' << question_state_count << "\n";
  for (const auto& item : graph.nodes()) {
    if (item.second.kind != NodeKind::Question || !included.count(item.first)) continue;
    out << "question_state " << std::quoted(item.first) << ' '
        << static_cast<unsigned>(graph.question_memory_state(item.first)) << ' '
        << graph.question_reference_count(item.first) << "\n";
  }
  out << "question_aspect_states";
  std::size_t aspect_count = 0;
  for (const auto& item : graph.nodes())
    if (item.second.kind == NodeKind::QuestionAspect && included.count(item.first)) ++aspect_count;
  out << ' ' << aspect_count << "\n";
  for (const auto& item : graph.nodes()) {
    if (item.second.kind != NodeKind::QuestionAspect || !included.count(item.first)) continue;
    out << "question_aspect_state " << std::quoted(item.first) << ' '
        << static_cast<unsigned>(graph.question_aspect_status(item.first)) << "\n";
  }
  out << "claim_lifecycle_states";
  std::size_t claim_count = 0;
  for (const auto& item : graph.nodes())
    if (item.second.kind == NodeKind::Claim && included.count(item.first)) ++claim_count;
  out << ' ' << claim_count << "\n";
  for (const auto& item : graph.nodes()) {
    if (item.second.kind != NodeKind::Claim || !included.count(item.first)) continue;
    out << "claim_status " << std::quoted(item.first) << ' '
        << static_cast<unsigned>(graph.claim_status(item.first)) << "\n";
  }

  if (!out) {
    if (error) *error = "CLUSTER_ARCHIVE_SERIALIZE_FAILED";
    return false;
  }
  *payload = out.str();
  if (error) error->clear();
  return true;
}

bool validate_cluster_archive_payload_identity(const std::string& payload,
    const std::string& expected_cluster_id, std::uint64_t expected_state_version,
    std::string* error) {
  auto fail = [&](const char* code) {
    if (error) *error = code;
    return false;
  };
  if (payload.empty() || expected_cluster_id.empty() || expected_state_version == 0)
    return fail("CLUSTER_ARCHIVE_PAYLOAD_IDENTITY_ARGUMENT_INVALID");
  std::istringstream lines(payload);
  std::string line;
  if (!std::getline(lines, line)) return fail("CLUSTER_ARCHIVE_PAYLOAD_FORMAT_UNSUPPORTED");
  {
    std::istringstream row(line);
    std::string marker, extra; unsigned version = 0;
    if (!(row >> marker >> version) || (row >> extra) ||
        marker != "SURGE_CLUSTER_ARCHIVE_PAYLOAD" || version != 2)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_FORMAT_UNSUPPORTED");
  }
  if (!std::getline(lines, line)) return fail("CLUSTER_ARCHIVE_PAYLOAD_WATERMARK_MISMATCH");
  {
    std::istringstream row(line);
    std::string key, extra; std::uint64_t watermark = 0;
    if (!(row >> key >> watermark) || (row >> extra) || key != "watermark" ||
        watermark != expected_state_version)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_WATERMARK_MISMATCH");
  }
  if (!std::getline(lines, line)) return fail("CLUSTER_ARCHIVE_PAYLOAD_CLUSTER_MISMATCH");
  {
    std::istringstream row(line);
    std::string key, cluster_id, extra;
    if (!(row >> key >> std::quoted(cluster_id)) || (row >> extra) ||
        key != "cluster" || cluster_id != expected_cluster_id)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_CLUSTER_MISMATCH");
  }
  if (!std::getline(lines, line)) return fail("CLUSTER_ARCHIVE_PAYLOAD_NODE_SECTION_INVALID");
  {
    std::istringstream row(line);
    std::string key, extra; std::size_t node_count = 0;
    if (!(row >> key >> node_count) || (row >> extra) || key != "nodes")
      return fail("CLUSTER_ARCHIVE_PAYLOAD_NODE_SECTION_INVALID");
  }
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
  if (!validate_cluster_archive_payload_identity(payload, cluster_id, state_version, error))
    return false;
  SnapshotDocument document;
  document.schema_id = kSchema; document.context_id = cluster_id;
  document.state_version = state_version; document.journal_sequence = journal_sequence;
  const std::size_t chunk_bytes = 64U * 1024U;
  std::size_t offset = 0;
  const SnapshotChunkReader reader = [&payload, &offset, chunk_bytes](
      std::string* chunk, bool* done) {
    if (!chunk || !done) return false;
    if (offset >= payload.size()) {
      chunk->clear();
      *done = true;
      return true;
    }
    const auto count = std::min(chunk_bytes, payload.size() - offset);
    chunk->assign(payload.data() + offset, count);
    offset += count;
    *done = false;
    return true;
  };
  return store_.save_stream(document, payload.size(), reader, error);
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
  if (!validate_cluster_archive_payload_identity(decoded.payload, expected_cluster_id,
      decoded.state_version, error)) return false;
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
  SnapshotDocument metadata;
  bool used_backup = false;
  std::size_t offset = 0;
  bool mismatch = false;
  const SnapshotPayloadSink sink = [&payload, &offset, &mismatch](
      const char* data, std::size_t size, std::string* sink_error) {
    if (offset > payload.size() || size > payload.size() - offset ||
        (size != 0 && std::char_traits<char>::compare(
            payload.data() + offset, data, size) != 0)) {
      mismatch = true;
      if (sink_error) *sink_error = "CLUSTER_ARCHIVE_VERIFY_MISMATCH";
      return false;
    }
    offset += size;
    return true;
  };
  if (!store_.load_stream(&metadata, sink, &used_backup, error)) return false;
  if (mismatch || offset != payload.size() ||
      metadata.schema_id != kSchema || metadata.context_id != cluster_id ||
      metadata.state_version != state_version ||
      metadata.journal_sequence != journal_sequence) {
    if (error) *error = "CLUSTER_ARCHIVE_VERIFY_MISMATCH";
    return false;
  }
  verified->cluster_id = metadata.context_id;
  verified->state_version = metadata.state_version;
  verified->journal_sequence = metadata.journal_sequence;
  verified->payload = payload;
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
  if (!validate_cluster_archive_payload_identity(payload, cluster_id,
      expected_state_version, &result.error)) return result;
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
