#include "surge/cluster_archive_store.hpp"
#include "surge/budgeted_context_graph.hpp"
#include <utility>
#include <set>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>
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
  // Include the transitive typed-reference closure. A one-hop pass is
  // insufficient when a referenced Claim/Evidence/Question itself references
  // another record: the resulting payload would contain a dangling ID.
  std::vector<std::string> reference_frontier(included.begin(), included.end());
  for (std::size_t cursor = 0; cursor < reference_frontier.size(); ++cursor) {
    const auto node = graph.nodes().find(reference_frontier[cursor]);
    if (node == graph.nodes().end()) continue;
    for (const auto& ref : node->second.references) {
      if (!graph.nodes().count(ref)) continue;
      if (included.insert(ref).second)
        reference_frontier.push_back(ref);
    }
  }

  // Keep validity-history provenance inside the archive closure too. A
  // validity record may be bound through a Supports relation rather than a
  // direct Claim reference, so typed-reference closure alone is insufficient.
  std::vector<std::string> history_frontier(included.begin(), included.end());
  for (std::size_t cursor = 0; cursor < history_frontier.size(); ++cursor) {
    const auto id = history_frontier[cursor];
    auto include_reference = [&](const std::string& reference) {
      if (!reference.empty() && graph.nodes().count(reference) &&
          included.insert(reference).second)
        history_frontier.push_back(reference);
    };
    const auto claim_history = graph.claim_validity_history().find(id);
    if (claim_history != graph.claim_validity_history().end()) {
      for (const auto& record : claim_history->second) {
        include_reference(record.validity.basis_evidence_id);
        include_reference(record.validity.review_evidence_id);
      }
    }
    const auto evidence_history = graph.evidence_validity_history().find(id);
    if (evidence_history != graph.evidence_validity_history().end()) {
      for (const auto& record : evidence_history->second) {
        include_reference(record.validity.basis_evidence_id);
        include_reference(record.validity.review_evidence_id);
      }
    }
    const auto node = graph.nodes().find(id);
    if (node != graph.nodes().end())
      for (const auto& reference : node->second.references)
        include_reference(reference);
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
    if (std::any_of(d.triggered.begin(), d.triggered.end(),
        [&](const DependencyTrigger& trigger) {
          return included.count(trigger.dependency_id) ||
                 included.count(trigger.prerequisite_id) ||
                 included.count(trigger.dependent_id);
        })) return true;
    return std::any_of(d.rejected_dependencies.begin(), d.rejected_dependencies.end(),
        [&](const std::string& id) { return included.count(id) != 0; });
  };
  // Dependency evaluation records are historical references too. Include
  // all connected endpoints and impacted/rejected dependencies in the closure.
  std::vector<std::string> dependency_frontier(included.begin(), included.end());
  for (std::size_t cursor = 0; cursor < dependency_frontier.size(); ++cursor) {
    const auto id = dependency_frontier[cursor];
    auto include_dependency_ref = [&](const std::string& reference) {
      if (!reference.empty() && graph.nodes().count(reference) &&
          included.insert(reference).second)
        dependency_frontier.push_back(reference);
    };
    for (const auto& item : graph.dependency_evaluations()) {
      const auto& d = item.second;
      const bool touches =
          std::find(d.impacted_nodes.begin(), d.impacted_nodes.end(), id) != d.impacted_nodes.end() ||
          std::find(d.rejected_dependencies.begin(), d.rejected_dependencies.end(), id) != d.rejected_dependencies.end() ||
          std::any_of(d.triggered.begin(), d.triggered.end(), [&](const DependencyTrigger& t) {
            return t.dependency_id == id || t.prerequisite_id == id || t.dependent_id == id;
          });
      if (!touches) continue;
      for (const auto& impacted : d.impacted_nodes) include_dependency_ref(impacted);
      for (const auto& rejected : d.rejected_dependencies) include_dependency_ref(rejected);
      for (const auto& trigger : d.triggered) {
        include_dependency_ref(trigger.dependency_id);
        include_dependency_ref(trigger.prerequisite_id);
        include_dependency_ref(trigger.dependent_id);
      }
    }
  }
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


bool validate_cluster_archive_payload_graph_records(const std::string& payload,
    std::string* error) {
  auto fail = [&](const char* code) {
    if (error) *error = code;
    return false;
  };
  std::istringstream input(payload);
  std::string line;
  if (!std::getline(input, line))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  if (!std::getline(input, line))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  std::uint64_t archive_watermark = 0;
  {
    std::istringstream watermark_row(line);
    std::string tag, extra;
    if (!(watermark_row >> tag >> archive_watermark) || tag != "watermark" ||
        archive_watermark == 0 || (watermark_row >> extra))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  }
  if (!std::getline(input, line) || !std::getline(input, line))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  auto parse_count = [](const std::string& row_text, const char* expected,
                        std::size_t& count) {
    std::istringstream row(row_text);
    std::string key, extra;
    if (!(row >> key >> count) || (row >> extra) || key != expected) return false;
    return true;
  };
  std::size_t node_count = 0;
  if (!parse_count(line, "nodes", node_count) || node_count > payload.size())
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  std::set<std::string> node_ids;
  std::map<std::string, unsigned> node_kinds;
  std::map<std::string, std::vector<std::string>> node_reference_map;
  std::vector<std::vector<std::string>> node_references;
  for (std::size_t i = 0; i < node_count; ++i) {
    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
    std::istringstream row(line);
    std::string tag, id, content, extra;
    unsigned kind = 0;
    std::size_t refs = 0;
    if (!(row >> tag >> std::quoted(id) >> kind >> std::quoted(content) >> refs) ||
        tag != "node" || id.empty() || kind > 9 || refs > payload.size() ||
        !node_ids.insert(id).second)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
    node_kinds.emplace(id, kind);
    std::vector<std::string> references;
    references.reserve(refs);
    for (std::size_t r = 0; r < refs; ++r) {
      std::string reference;
      if (!(row >> std::quoted(reference)) || reference.empty())
        return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
      references.push_back(std::move(reference));
    }
    if (row >> extra)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
    node_reference_map.emplace(id, references);
    node_references.push_back(std::move(references));
  }
  for (const auto& references : node_references)
    for (const auto& reference : references)
      if (!node_ids.count(reference))
        return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  if (!std::getline(input, line))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  std::size_t relation_count = 0;
  if (!parse_count(line, "relations", relation_count) || relation_count > payload.size())
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  std::set<std::string> relation_ids;
  std::set<std::pair<std::string, std::string>> support_edges;
  for (std::size_t i = 0; i < relation_count; ++i) {
    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
    std::istringstream row(line);
    std::string tag, id, from, to, extra;
    unsigned kind = 0;
    if (!(row >> tag >> std::quoted(id) >> kind >> std::quoted(from) >>
          std::quoted(to)) || tag != "relation" || id.empty() || kind > 9 ||
        from.empty() || to.empty() || !node_ids.count(from) || !node_ids.count(to) ||
        !relation_ids.insert(id).second || (row >> extra))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
    if (kind == static_cast<unsigned>(RelationKind::Supports))
      support_edges.emplace(from, to);
  }
  if (!std::getline(input, line))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  unsigned has_focus = 0;
  {
    std::istringstream row(line);
    std::string tag, extra;
    if (!(row >> tag >> has_focus) || (row >> extra) ||
        tag != "has_focus" || has_focus > 1)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  }
  auto validate_focus = [&](const std::string& focus_line) {
    std::istringstream row(focus_line);
    std::string tag, cluster, interest, extra;
    unsigned chaos = 0;
    if (!(row >> tag >> std::quoted(cluster) >> std::quoted(interest) >> chaos) ||
        tag != "focus" || cluster.empty() || !node_ids.count(cluster) ||
        (node_kinds.at(cluster) != static_cast<unsigned>(NodeKind::Cluster) &&
         node_kinds.at(cluster) != static_cast<unsigned>(NodeKind::ChaosCluster)) ||
        (!interest.empty() &&
         (!node_ids.count(interest) ||
          node_kinds.at(interest) != static_cast<unsigned>(NodeKind::Interest) ||
          node_reference_map.at(interest).size() != 2 ||
          node_reference_map.at(interest)[1] != cluster)) ||
        chaos > 1 || (row >> extra))
      return false;
    if ((node_kinds.at(cluster) == static_cast<unsigned>(NodeKind::ChaosCluster)) !=
        (chaos != 0))
      return false;
    return true;
  };
  if (has_focus && (!std::getline(input, line) || !validate_focus(line)))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  if (!std::getline(input, line))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  std::size_t focus_history_count = 0;
  if (!parse_count(line, "focus_history", focus_history_count) ||
      focus_history_count > payload.size())
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  for (std::size_t i = 0; i < focus_history_count; ++i) {
    if (!std::getline(input, line) || !validate_focus(line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  }

  auto parse_string_vector = [&](const std::string& text, const char* expected,
                                 std::vector<std::string>& values) {
    std::istringstream row(text);
    std::string tag, value, extra;
    std::size_t count = 0;
    if (!(row >> tag >> count) || tag != expected || count > payload.size())
      return false;
    values.clear();
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      if (!(row >> std::quoted(value)) || value.empty()) return false;
      values.push_back(value);
    }
    return !(row >> extra);
  };
  auto valid_digest = [](const std::string& digest) {
    return digest.size() == 64 &&
        std::all_of(digest.begin(), digest.end(), [](unsigned char c) {
          return std::isxdigit(c) != 0;
        });
  };
  auto parse_anchor = [&](std::istringstream& row, const std::string& source_digest,
                          const std::string& offset_basis, ScopeAnchor& anchor) {
    return static_cast<bool>(row >> std::quoted(anchor.anchor_id) >>
        std::quoted(anchor.source_digest) >> std::quoted(anchor.offset_basis) >>
        anchor.start_offset >> anchor.end_offset >> std::quoted(anchor.exact_text) >>
        std::quoted(anchor.packet_ref)) &&
        !anchor.anchor_id.empty() && anchor.source_digest == source_digest &&
        anchor.offset_basis == offset_basis && anchor.offset_basis == "UTF8_BYTE_NFC" &&
        anchor.start_offset < anchor.end_offset && !anchor.exact_text.empty() &&
        !anchor.packet_ref.empty();
  };
  if (!std::getline(input, line))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
  std::size_t scope_count = 0;
  if (!parse_count(line, "scope_spans", scope_count) || scope_count > payload.size())
    return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPANS_INVALID");
  std::set<std::string> scope_ids;
  for (std::size_t i = 0; i < scope_count; ++i) {
    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    std::istringstream row(line);
    std::string tag, id, source_digest, offset_basis, extra;
    unsigned kind = 0, has_closer = 0;
    ScopeAnchor opener, closer;
    if (!(row >> tag >> std::quoted(id) >> kind >> std::quoted(source_digest) >>
          std::quoted(offset_basis)) || tag != "scope" || id.empty() ||
        !scope_ids.insert(id).second ||
        kind > static_cast<unsigned>(ScopeKind::TableHeader) ||
        !valid_digest(source_digest) || offset_basis != "UTF8_BYTE_NFC" ||
        !parse_anchor(row, source_digest, offset_basis, opener) ||
        !(row >> has_closer) || has_closer > 1)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    if (has_closer) {
      if (!parse_anchor(row, source_digest, offset_basis, closer) ||
          closer.anchor_id == opener.anchor_id ||
          closer.start_offset < opener.end_offset)
        return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    }
    if (row >> extra)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");

    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    std::vector<std::string> targets;
    {
      // The target vector is encoded as a bare count followed by quoted IDs.
      std::istringstream vector_row(line);
      std::size_t count = 0;
      std::string value, trailing;
      if (!(vector_row >> count) || count > payload.size())
        return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
      targets.reserve(count);
      for (std::size_t j = 0; j < count; ++j) {
        if (!(vector_row >> std::quoted(value)) || value.empty())
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        targets.push_back(value);
      }
      if (vector_row >> trailing)
        return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    }
    for (const auto& ref : targets)
      if (!node_ids.count(ref))
        return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");

    if (!std::getline(input, line) || line.rfind("scope_antecedents ", 0) != 0)
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    std::vector<std::string> antecedents;
    if (!parse_string_vector(line, "scope_antecedents", antecedents))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    for (const auto& ref : antecedents)
      if (!node_ids.count(ref))
        return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");

    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    std::istringstream meta(line);
    std::string meta_tag, attribution, modality, temporal_role, parent;
    if (!(meta >> meta_tag >> std::quoted(attribution) >> std::quoted(modality) >>
          std::quoted(temporal_role) >> std::quoted(parent)) ||
        meta_tag != "scope_meta")
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    std::vector<std::string> packets;
    std::string packet_count_text;
    std::getline(meta, packet_count_text);
    if (packet_count_text.empty()) return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    std::istringstream packet_row(packet_count_text);
    std::size_t packet_count = 0;
    if (!(packet_row >> packet_count) || packet_count == 0 ||
        packet_count > payload.size())
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    for (std::size_t j = 0; j < packet_count; ++j) {
      std::string packet;
      if (!(packet_row >> std::quoted(packet)) || packet.empty())
        return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
      packets.push_back(std::move(packet));
    }
    if (packet_row >> extra ||
        std::find(packets.begin(), packets.end(), opener.packet_ref) == packets.end() ||
        (has_closer &&
         std::find(packets.begin(), packets.end(), closer.packet_ref) == packets.end()))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");

    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    std::istringstream result(line);
    std::string result_tag;
    unsigned status = 0, effect = 0, unconditional = 0;
    if (!(result >> result_tag >> status >> effect >> unconditional) ||
        result_tag != "scope_result" ||
        status > static_cast<unsigned>(ScopeStatus::Unresolved) ||
        effect > static_cast<unsigned>(ScopeEffect::TableHeaderScope) ||
        unconditional > 1 || (result >> extra))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    const auto scope_kind = static_cast<ScopeKind>(kind);
    auto targets_are = [&](std::size_t expected, NodeKind expected_kind) {
      if (targets.size() != expected) return false;
      for (const auto& ref : targets)
        if (node_kinds.at(ref) != static_cast<unsigned>(expected_kind)) return false;
      return true;
    };
    auto antecedents_are_entities = [&]() {
      if (antecedents.empty()) return false;
      for (const auto& ref : antecedents)
        if (node_kinds.at(ref) != static_cast<unsigned>(NodeKind::Entity)) return false;
      return true;
    };
    switch (scope_kind) {
      case ScopeKind::Negation:
        if (!targets_are(1, NodeKind::Claim) || effect != static_cast<unsigned>(ScopeEffect::NegatedClaim))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::Condition:
        if (!targets_are(2, NodeKind::Claim) || effect != static_cast<unsigned>(ScopeEffect::ConditionalRelation))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::Modality:
        if (!targets_are(1, NodeKind::Claim) || modality.empty() ||
            effect != static_cast<unsigned>(ScopeEffect::ModalityQualified))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::Concession:
        if (!targets_are(2, NodeKind::Claim) || effect != static_cast<unsigned>(ScopeEffect::ConcessionRelation))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::Contrast:
        if (!targets_are(2, NodeKind::Claim) || effect != static_cast<unsigned>(ScopeEffect::ContrastRelation))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::Coreference:
      case ScopeKind::Anaphora:
        if (targets.size() != 1 || !antecedents_are_entities() ||
            effect != static_cast<unsigned>(ScopeEffect::ReferenceCandidate))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::Quotation:
        if (effect != static_cast<unsigned>(ScopeEffect::QuotationEvidence))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::Temporal:
        if (!targets_are(1, NodeKind::Claim) || temporal_role.empty() ||
            effect != static_cast<unsigned>(ScopeEffect::TemporalQualifier))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::ListContinuation:
        if (parent.empty() || effect != static_cast<unsigned>(ScopeEffect::StructuralContinuation))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
      case ScopeKind::TableHeader:
        if (parent.empty() || effect != static_cast<unsigned>(ScopeEffect::TableHeaderScope))
          return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
        break;
    }
    if ((scope_kind == ScopeKind::Condition || scope_kind == ScopeKind::Concession ||
         scope_kind == ScopeKind::Contrast || scope_kind == ScopeKind::Quotation) &&
        !has_closer && status != static_cast<unsigned>(ScopeStatus::Unresolved))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    if (!attribution.empty() &&
        (!node_ids.count(attribution) ||
         node_kinds.at(attribution) != static_cast<unsigned>(NodeKind::Entity)))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    if (!parent.empty() && !node_ids.count(parent))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    if ((scope_kind == ScopeKind::Modality && modality.empty()) ||
        (scope_kind == ScopeKind::Temporal && temporal_role.empty()) ||
        ((scope_kind == ScopeKind::ListContinuation ||
          scope_kind == ScopeKind::TableHeader) && parent.empty()))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
  }

  auto parse_history_header = [&](const char* expected, std::size_t& count) {
    return std::getline(input, line) && parse_count(line, expected, count) &&
        count <= node_count;
  };
  auto valid_rfc3339 = [](const std::string& value) {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':')
      return false;
    for (std::size_t i : {0U,1U,2U,3U,5U,6U,8U,9U,11U,12U,14U,15U,17U,18U})
      if (value[i] < '0' || value[i] > '9') return false;
    if (value.size() == 20 && value[19] == 'Z') return true;
    if (value.size() == 25 && (value[19] == '+' || value[19] == '-') &&
        value[22] == ':') {
      for (std::size_t i : {20U,21U,23U,24U})
        if (value[i] < '0' || value[i] > '9') return false;
      return true;
    }
    return false;
  };
  auto parse_validity_record = [&](const std::string& record_tag,
      const std::string& owner, NodeKind owner_kind, std::uint64_t& previous_revision,
      std::uint64_t& previous_version, bool claim_record) {
    if (!std::getline(input, line)) return false;
    std::istringstream row(line);
    std::string tag, subject, valid_from, valid_until, basis, review, reviewer, extra;
    std::uint64_t revision = 0, recorded_version = 0;
    if (!(row >> tag >> std::quoted(subject) >> std::quoted(valid_from) >>
          std::quoted(valid_until) >> std::quoted(basis) >> revision >>
          std::quoted(review) >> std::quoted(reviewer) >> recorded_version) ||
        tag != record_tag || subject != owner || subject.empty() ||
        !node_ids.count(subject) ||
        node_kinds.at(subject) != static_cast<unsigned>(owner_kind) ||
        !valid_rfc3339(valid_from) ||
        (!valid_until.empty() && !valid_rfc3339(valid_until)) ||
        basis.empty() || !node_ids.count(basis) ||
        node_kinds.at(basis) != static_cast<unsigned>(NodeKind::Evidence) ||
        (review.empty() != reviewer.empty()) ||
        (!review.empty() && (!node_ids.count(review) ||
          node_kinds.at(review) != static_cast<unsigned>(NodeKind::Evidence))) ||
        revision != previous_revision + 1 || recorded_version == 0 ||
        recorded_version > archive_watermark || recorded_version <= previous_version ||
        (row >> extra))
      return false;
    const auto& refs = node_reference_map.at(subject);
    const bool directly_linked = std::find(refs.begin(), refs.end(), basis) != refs.end();
    const bool relation_linked = support_edges.count({basis, subject}) != 0;
    if (claim_record ? (!directly_linked && !relation_linked)
                     : (basis != subject && !relation_linked))
      return false;
    previous_revision = revision;
    previous_version = recorded_version;
    return true;
  };
  std::size_t claim_history_count = 0;
  if (!parse_history_header("claim_validity_histories", claim_history_count))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_CLAIM_VALIDITY_INVALID");
  std::set<std::string> claim_history_ids;
  for (std::size_t i = 0; i < claim_history_count; ++i) {
    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_CLAIM_VALIDITY_INVALID");
    std::istringstream row(line);
    std::string tag, owner, extra;
    std::size_t count = 0;
    if (!(row >> tag >> std::quoted(owner) >> count) || tag != "claim_history" ||
        owner.empty() || !node_ids.count(owner) ||
        node_kinds.at(owner) != static_cast<unsigned>(NodeKind::Claim) ||
        count == 0 || count > payload.size() ||
        !claim_history_ids.insert(owner).second || (row >> extra))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_CLAIM_VALIDITY_INVALID");
    std::uint64_t revision = 0, recorded_version = 0;
    for (std::size_t j = 0; j < count; ++j)
      if (!parse_validity_record("claim_validity", owner, NodeKind::Claim,
                                 revision, recorded_version, true))
        return fail("CLUSTER_ARCHIVE_PAYLOAD_CLAIM_VALIDITY_INVALID");
  }

  std::size_t evidence_history_count = 0;
  if (!parse_history_header("evidence_validity_histories", evidence_history_count))
    return fail("CLUSTER_ARCHIVE_PAYLOAD_EVIDENCE_VALIDITY_INVALID");
  std::set<std::string> evidence_history_ids;
  for (std::size_t i = 0; i < evidence_history_count; ++i) {
    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_EVIDENCE_VALIDITY_INVALID");
    std::istringstream row(line);
    std::string tag, owner, extra;
    std::size_t count = 0;
    if (!(row >> tag >> std::quoted(owner) >> count) || tag != "evidence_history" ||
        owner.empty() || !node_ids.count(owner) ||
        node_kinds.at(owner) != static_cast<unsigned>(NodeKind::Evidence) ||
        count == 0 || count > payload.size() ||
        !evidence_history_ids.insert(owner).second || (row >> extra))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_EVIDENCE_VALIDITY_INVALID");
    std::uint64_t revision = 0, recorded_version = 0;
    for (std::size_t j = 0; j < count; ++j)
      if (!parse_validity_record("evidence_validity", owner, NodeKind::Evidence,
                                 revision, recorded_version, false))
        return fail("CLUSTER_ARCHIVE_PAYLOAD_EVIDENCE_VALIDITY_INVALID");
  }

  std::size_t dependency_count = 0;
  if (!std::getline(input, line) ||
      !parse_count(line, "dependency_evaluations", dependency_count) ||
      dependency_count > payload.size())
    return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_EVALUATION_INVALID");
  std::set<std::string> dependency_record_ids;
  for (std::size_t i = 0; i < dependency_count; ++i) {
    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_EVALUATION_INVALID");
    std::istringstream header(line);
    std::string tag, record_id, delta_id, extra;
    std::size_t trigger_count = 0;
    if (!(header >> tag >> std::quoted(record_id) >> std::quoted(delta_id) >> trigger_count) ||
        tag != "dependency" || record_id.empty() || delta_id.empty() ||
        record_id != delta_id || trigger_count > payload.size() ||
        !dependency_record_ids.insert(record_id).second || (header >> extra))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_EVALUATION_INVALID");
    std::set<std::string> triggered_ids;
    for (std::size_t j = 0; j < trigger_count; ++j) {
      if (!std::getline(input, line))
        return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_TRIGGER_INVALID");
      std::istringstream trigger_row(line);
      std::string trigger_tag, dependency_id, prerequisite_id, dependent_id, trailing;
      if (!(trigger_row >> trigger_tag >> std::quoted(dependency_id) >>
            std::quoted(prerequisite_id) >> std::quoted(dependent_id)) ||
          trigger_tag != "trigger" || dependency_id.empty() ||
          prerequisite_id.empty() || dependent_id.empty() ||
          (trigger_row >> trailing) || !triggered_ids.insert(dependency_id).second ||
          !node_ids.count(dependency_id) || !node_ids.count(prerequisite_id) ||
          !node_ids.count(dependent_id) ||
          node_kinds.at(dependency_id) != static_cast<unsigned>(NodeKind::Dependency))
        return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_TRIGGER_INVALID");
      const auto& refs = node_reference_map.at(dependency_id);
      if (refs.size() != 2 || refs[0] != prerequisite_id || refs[1] != dependent_id ||
          prerequisite_id == dependent_id)
        return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_TRIGGER_INVALID");
    }
    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_IMPACT_INVALID");
    std::vector<std::string> impacted;
    if (!parse_string_vector(line, "impacted", impacted))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_IMPACT_INVALID");
    std::set<std::string> impacted_ids;
    for (const auto& id : impacted)
      if (!node_ids.count(id) || !impacted_ids.insert(id).second)
        return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_IMPACT_INVALID");
    if (!std::getline(input, line))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_REJECTION_INVALID");
    std::vector<std::string> rejected;
    if (!parse_string_vector(line, "rejected", rejected))
      return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_REJECTION_INVALID");
    std::set<std::string> rejected_ids;
    for (const auto& id : rejected)
      if (!node_ids.count(id) ||
          node_kinds.at(id) != static_cast<unsigned>(NodeKind::Dependency) ||
          !rejected_ids.insert(id).second || triggered_ids.count(id))
        return fail("CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_REJECTION_INVALID");
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
  if (!validate_cluster_archive_payload_identity(payload, cluster_id, state_version, error) ||
      !validate_cluster_archive_payload_graph_records(payload, error))
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
      decoded.state_version, error) ||
      !validate_cluster_archive_payload_graph_records(decoded.payload, error)) return false;
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
