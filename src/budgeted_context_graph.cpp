#include "surge/budgeted_context_graph.hpp"
#include <limits>
#include <utility>

namespace surge {
namespace {
constexpr std::size_t kEntryOverhead = 256;

bool add(std::size_t value, std::size_t& total) {
  if (value > std::numeric_limits<std::size_t>::max() - total) return false;
  total += value;
  return true;
}

bool add_node(const GraphNode& node, std::size_t& total) {
  if (!add(kEntryOverhead, total) || !add(node.id.size(), total) ||
      !add(node.content.size(), total)) return false;
  for (const auto& ref : node.references)
    if (!add(sizeof(std::string), total) || !add(ref.size(), total)) return false;
  return true;
}

bool add_relation(const GraphRelation& relation, std::size_t& total) {
  return add(kEntryOverhead, total) && add(relation.id.size(), total) &&
      add(relation.from.size(), total) && add(relation.to.size(), total);
}

bool add_scope(const AddScopeSpan& scope, std::size_t& total) {
  const auto& p = scope.span;
  if (!add(kEntryOverhead, total) || !add(p.span_id.size(), total) ||
      !add(p.source_digest.size(), total) || !add(p.offset_basis.size(), total) ||
      !add(p.opener.anchor_id.size(), total) ||
      !add(p.opener.source_digest.size(), total) ||
      !add(p.opener.offset_basis.size(), total) ||
      !add(p.opener.exact_text.size(), total) ||
      !add(p.opener.packet_ref.size(), total) ||
      !add(p.attribution_ref.size(), total) || !add(p.modality_kind.size(), total) ||
      !add(p.temporal_role.size(), total) || !add(p.parent_ref.size(), total) ||
      !add(scope.source_snapshot.size(), total)) return false;
  if (p.closer && (!add(p.closer->anchor_id.size(), total) ||
      !add(p.closer->source_digest.size(), total) ||
      !add(p.closer->offset_basis.size(), total) ||
      !add(p.closer->exact_text.size(), total) ||
      !add(p.closer->packet_ref.size(), total))) return false;
  for (const auto& ref : p.target_refs) if (!add(ref.size(), total)) return false;
  for (const auto& ref : p.antecedent_refs) if (!add(ref.size(), total)) return false;
  for (const auto& ref : p.packet_refs) if (!add(ref.size(), total)) return false;
  return true;
}
}  // namespace

BudgetedContextGraph::BudgetedContextGraph(ContextGraph& graph,
    ResourceBudget& budget, std::size_t max_staging_bytes)
    : graph_(graph), budget_(budget), max_staging_bytes_(max_staging_bytes) {}

bool BudgetedContextGraph::estimate_delta(const SemanticDelta& delta,
    std::size_t& bytes, std::size_t& clusters, std::string& error) {
  bytes = 0; clusters = 0; error.clear();
  for (const auto& item : delta.nodes) {
    if (!add_node(item.node, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
    if (item.node.kind == NodeKind::Cluster || item.node.kind == NodeKind::ChaosCluster)
      if (!add(1, clusters)) { error = "CONTEXT_GRAPH_CLUSTER_COUNT_OVERFLOW"; return false; }
  }
  for (const auto& item : delta.relations)
    if (!add_relation(item.relation, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  for (const auto& item : delta.scope_spans)
    if (!add_scope(item, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  for (const auto& item : delta.claim_validity_updates) {
    const auto& v = item;
    if (!add(kEntryOverhead, bytes) || !add(v.claim_id.size(), bytes) ||
        !add(v.valid_from.size(), bytes) || !add(v.valid_until.size(), bytes) ||
        !add(v.basis_evidence_id.size(), bytes) || !add(v.review_evidence_id.size(), bytes) ||
        !add(v.reviewer_id.size(), bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  for (const auto& item : delta.evidence_validity_updates) {
    const auto& v = item;
    if (!add(kEntryOverhead, bytes) || !add(v.evidence_id.size(), bytes) ||
        !add(v.valid_from.size(), bytes) || !add(v.valid_until.size(), bytes) ||
        !add(v.basis_evidence_id.size(), bytes) || !add(v.review_evidence_id.size(), bytes) ||
        !add(v.reviewer_id.size(), bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  if (delta.focus && (!add(kEntryOverhead, bytes) ||
      !add(delta.focus->focus.cluster_id.size(), bytes) ||
      !add(delta.focus->focus.interest_id.size(), bytes))) {
    error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
  }
  return true;
}

bool BudgetedContextGraph::estimate_graph(const ContextGraph& graph,
    std::size_t& bytes, std::size_t& clusters, std::string& error) {
  bytes = 0; clusters = 0; error.clear();
  for (const auto& item : graph.nodes()) {
    if (!add_node(item.second, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
    if (item.second.kind == NodeKind::Cluster || item.second.kind == NodeKind::ChaosCluster)
      if (!add(1, clusters)) { error = "CONTEXT_GRAPH_CLUSTER_COUNT_OVERFLOW"; return false; }
  }
  for (const auto& item : graph.relations())
    if (!add_relation(item.second, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  for (const auto& item : graph.scope_spans()) {
    AddScopeSpan span{item.second.proposal, {}};
    if (!add_scope(span, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  return true;
}

bool BudgetedContextGraph::recover(std::string& error) {
  error.clear();
  if (recovered_) {
    error = "CONTEXT_GRAPH_BUDGET_ALREADY_RECOVERED"; return false;
  }
  std::size_t bytes = 0, clusters = 0;
  if (!estimate_graph(graph_, bytes, clusters, error)) return false;
  ResourceLease resident, retained_clusters;
  if (bytes && !budget_.try_acquire(ResourceKind::ResidentBytes, bytes, resident, error))
    return false;
  if (clusters && !budget_.try_acquire(ResourceKind::RetainedClusters, clusters,
                                        retained_clusters, error)) return false;
  if (resident) resident_leases_.push_back(std::move(resident));
  if (retained_clusters) cluster_leases_.push_back(std::move(retained_clusters));
  accounted_resident_bytes_ = bytes;
  recovered_ = true;
  return true;
}

DeltaResult BudgetedContextGraph::apply(const SemanticDelta& delta) {
  std::size_t bytes = 0, clusters = 0;
  std::string error;
  if (!estimate_delta(delta, bytes, clusters, error))
    return {false, false, graph_.state_version(), error};

  std::vector<JournalEvent> prepared;
  ResourceLease staging, resident, retained_clusters;
  if (max_staging_bytes_ != 0) {
    const auto estimate = bytes > std::numeric_limits<std::size_t>::max() / 2
        ? std::numeric_limits<std::size_t>::max() : bytes * 2;
    if (estimate > max_staging_bytes_)
      return {false, false, graph_.state_version(), "CONTEXT_GRAPH_STAGING_BUDGET_EXCEEDED"};
    if (estimate && !budget_.try_acquire(ResourceKind::TransientBytes, estimate,
                                          staging, error))
      return {false, false, graph_.state_version(), error};
  }
  if (!graph_.prepare_events(delta, prepared, error))
    return {false, false, graph_.state_version(), error};
  if (prepared.empty()) return graph_.apply(delta);

  if (bytes && !budget_.try_acquire(ResourceKind::ResidentBytes, bytes, resident, error))
    return {false, false, graph_.state_version(), error};
  if (clusters && !budget_.try_acquire(ResourceKind::RetainedClusters, clusters,
                                        retained_clusters, error))
    return {false, false, graph_.state_version(), error};

  // Reserve lease-vector capacity before the durable journal commit. Publishing
  // leases after commit then consists only of noexcept moves into existing slots.
  try {
    if (resident) resident_leases_.reserve(resident_leases_.size() + 1);
    if (retained_clusters) cluster_leases_.reserve(cluster_leases_.size() + 1);
  } catch (...) {
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_LEASE_STAGING_FAILED"};
  }

  auto result = graph_.apply(delta);
  if (!result.committed || result.duplicate) return result;
  if (resident) resident_leases_.push_back(std::move(resident));
  if (retained_clusters) cluster_leases_.push_back(std::move(retained_clusters));
  accounted_resident_bytes_ += bytes;
  return result;
}

bool BudgetedContextGraph::reserve_mutation(std::size_t bytes, std::size_t events,
    ResourceLease& resident, ResourceLease& retained, std::string& error) {
  if (bytes && !budget_.try_acquire(ResourceKind::ResidentBytes, bytes, resident, error))
    return false;
  if (events && !budget_.try_acquire(ResourceKind::RetainedEvents, events, retained, error)) {
    resident.reset(); return false;
  }
  try {
    if (resident) mutation_resident_leases_.reserve(mutation_resident_leases_.size()+1);
    if (retained) mutation_event_leases_.reserve(mutation_event_leases_.size()+1);
  } catch (...) {
    resident.reset(); retained.reset();
    error = "CONTEXT_GRAPH_MUTATION_LEASE_STAGING_FAILED";
    return false;
  }
  return true;
}

void BudgetedContextGraph::publish_mutation(ResourceLease&& resident,
                                            ResourceLease&& retained) {
  if (resident) mutation_resident_leases_.push_back(std::move(resident));
  if (retained) mutation_event_leases_.push_back(std::move(retained));
}

bool BudgetedContextGraph::record_question_reference(const std::string& id,
    std::uint64_t expected, bool& reactivated, std::string& error) {
  ResourceLease resident, retained;
  if (!reserve_mutation(512 + id.size(), 1, resident, retained, error)) return false;
  const auto before = graph_.state_version();
  if (!graph_.record_question_reference(id, expected, reactivated, error)) return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

bool BudgetedContextGraph::record_question_reference_with_dependents(
    const std::string& id, std::uint64_t expected, std::size_t max_depth,
    std::size_t max_nodes, bool& reactivated, std::size_t& reactivated_dependents,
    std::string& error) {
  // The traversal is bounded by max_nodes; reserve its maximum temporary and
  // durable footprint before allowing the underlying journal commit.
  if (max_nodes > (std::numeric_limits<std::size_t>::max()-512)/512) {
    error = "CONTEXT_GRAPH_MUTATION_SIZE_OVERFLOW"; return false;
  }
  ResourceLease staging, resident, retained;
  const auto bytes = 512 + id.size() + max_nodes * 512;
  if (max_staging_bytes_ && bytes > max_staging_bytes_) {
    error = "CONTEXT_GRAPH_STAGING_BUDGET_EXCEEDED"; return false;
  }
  if (max_staging_bytes_ &&
      !budget_.try_acquire(ResourceKind::TransientBytes, bytes, staging, error)) return false;
  if (!reserve_mutation(512 + id.size() + max_nodes * 64, 1, resident, retained, error))
    return false;
  const auto before = graph_.state_version();
  if (!graph_.record_question_reference_with_dependents(id, expected, max_depth,
      max_nodes, reactivated, reactivated_dependents, error)) return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

bool BudgetedContextGraph::set_question_memory_state(const std::string& id,
    QuestionMemoryState state, std::uint64_t expected, std::string& error) {
  ResourceLease resident, retained;
  if (!reserve_mutation(512 + id.size(), 1, resident, retained, error)) return false;
  const auto before = graph_.state_version();
  if (!graph_.set_question_memory_state(id, state, expected, error)) return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

bool BudgetedContextGraph::enforce_question_memory_capacity(
    const std::vector<std::pair<std::string,double>>& scores, std::size_t max_active,
    std::uint64_t expected, std::vector<std::string>& demoted, std::string& error) {
  if (scores.size() > std::numeric_limits<std::size_t>::max()/512) {
    error = "CONTEXT_GRAPH_MUTATION_SIZE_OVERFLOW"; return false;
  }
  const auto bytes = 512 + scores.size()*512;
  ResourceLease staging, resident, retained;
  if (max_staging_bytes_ && bytes > max_staging_bytes_) {
    error = "CONTEXT_GRAPH_STAGING_BUDGET_EXCEEDED"; return false;
  }
  if (max_staging_bytes_ &&
      !budget_.try_acquire(ResourceKind::TransientBytes, bytes, staging, error)) return false;
  if (!reserve_mutation(bytes, scores.size(), resident, retained, error)) return false;
  const auto before = graph_.state_version();
  if (!graph_.enforce_question_memory_capacity(scores, max_active, expected, demoted, error))
    return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

bool BudgetedContextGraph::set_question_aspect_status(const std::string& id,
    QuestionAspectStatus status, std::uint64_t expected, std::string& error) {
  ResourceLease resident, retained;
  if (!reserve_mutation(512 + id.size(), 1, resident, retained, error)) return false;
  const auto before = graph_.state_version();
  if (!graph_.set_question_aspect_status(id, status, expected, error)) return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

bool BudgetedContextGraph::apply_question_aspect_statuses(
    const std::vector<std::pair<std::string,QuestionAspectStatus>>& updates,
    std::uint64_t expected, std::string& error) {
  if (updates.size() > std::numeric_limits<std::size_t>::max()/1024) {
    error = "CONTEXT_GRAPH_MUTATION_SIZE_OVERFLOW"; return false;
  }
  std::size_t bytes = 512;
  for (const auto& update : updates) {
    if (update.first.size() > std::numeric_limits<std::size_t>::max()-bytes-512) {
      error = "CONTEXT_GRAPH_MUTATION_SIZE_OVERFLOW"; return false;
    }
    bytes += 512 + update.first.size();
  }
  ResourceLease staging, resident, retained;
  if (max_staging_bytes_ && bytes > max_staging_bytes_) {
    error = "CONTEXT_GRAPH_STAGING_BUDGET_EXCEEDED"; return false;
  }
  if (max_staging_bytes_ &&
      !budget_.try_acquire(ResourceKind::TransientBytes, bytes, staging, error)) return false;
  if (!reserve_mutation(bytes, updates.size(), resident, retained, error)) return false;
  const auto before = graph_.state_version();
  if (!graph_.apply_question_aspect_statuses(updates, expected, error)) return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

bool BudgetedContextGraph::reassess_claim(const std::string& claim_id,
    const std::string& reference_at, std::uint64_t expected,
    TemporalReassessmentRecord& result, std::string& error) {
  if (claim_id.size() > std::numeric_limits<std::size_t>::max()-reference_at.size()-2048) {
    error = "CONTEXT_GRAPH_MUTATION_SIZE_OVERFLOW"; return false;
  }
  const auto bytes = 2048 + claim_id.size() + reference_at.size();
  ResourceLease staging, resident, retained;
  if (max_staging_bytes_ && bytes > max_staging_bytes_) {
    error = "CONTEXT_GRAPH_STAGING_BUDGET_EXCEEDED"; return false;
  }
  if (max_staging_bytes_ &&
      !budget_.try_acquire(ResourceKind::TransientBytes, bytes, staging, error)) return false;
  if (!reserve_mutation(bytes, 1, resident, retained, error)) return false;
  const auto before = graph_.state_version();
  if (!graph_.reassess_claim(claim_id, reference_at, expected, result, error)) return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

}  // namespace surge
