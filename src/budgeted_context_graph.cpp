#include "surge/budgeted_context_graph.hpp"
#include "surge/cluster_retention.hpp"
#include "surge/sha256.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <initializer_list>
#include <utility>

namespace surge {
namespace {
constexpr std::size_t kEntryOverhead = 256;

bool add(std::size_t value, std::size_t& total) {
  if (value > std::numeric_limits<std::size_t>::max() - total) return false;
  total += value;
  return true;
}

bool add_parts(std::size_t& total, std::initializer_list<std::size_t> parts) {
  for (const auto part : parts)
    if (!add(part, total)) return false;
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
    std::size_t& bytes, std::size_t& clusters, std::size_t& retained_events,
    std::string& error) {
  bytes = 0; clusters = 0; retained_events = 0; error.clear();
  for (const auto& item : delta.nodes) {
    if (!add(1, retained_events)) { error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false; }
    if (!add_node(item.node, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
    if (item.node.kind == NodeKind::Cluster || item.node.kind == NodeKind::ChaosCluster)
      if (!add(1, clusters)) { error = "CONTEXT_GRAPH_CLUSTER_COUNT_OVERFLOW"; return false; }
  }
  for (const auto& item : delta.relations) {
    if (!add(1, retained_events)) { error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false; }
    if (!add_relation(item.relation, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  for (const auto& id : delta.remove_relation_ids) {
    if (!add(1, retained_events) || !add(kEntryOverhead, bytes) ||
        !add(id.size(), bytes)) {
      error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
    }
  }
  for (const auto& id : delta.reclaim_entity_node_ids) {
    if (!add(1, retained_events) || !add(kEntryOverhead, bytes) ||
        !add(id.size(), bytes)) {
      error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
    }
  }
  for (const auto& item : delta.scope_spans) {
    if (!add(1, retained_events)) { error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false; }
    if (!add_scope(item, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  for (const auto& item : delta.claim_validity_updates) {
    if (!add(1, retained_events)) { error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false; }
    const auto& v = item;
    if (!add(kEntryOverhead, bytes) || !add(v.claim_id.size(), bytes) ||
        !add(v.valid_from.size(), bytes) || !add(v.valid_until.size(), bytes) ||
        !add(v.basis_evidence_id.size(), bytes) || !add(v.review_evidence_id.size(), bytes) ||
        !add(v.reviewer_id.size(), bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  for (const auto& item : delta.evidence_validity_updates) {
    if (!add(1, retained_events)) { error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false; }
    const auto& v = item;
    if (!add(kEntryOverhead, bytes) || !add(v.evidence_id.size(), bytes) ||
        !add(v.valid_from.size(), bytes) || !add(v.valid_until.size(), bytes) ||
        !add(v.basis_evidence_id.size(), bytes) || !add(v.review_evidence_id.size(), bytes) ||
        !add(v.reviewer_id.size(), bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  if (delta.focus && (!add(1, retained_events) || !add(kEntryOverhead, bytes) ||
      !add(delta.focus->focus.cluster_id.size(), bytes) ||
      !add(delta.focus->focus.interest_id.size(), bytes))) {
    error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
  }
  return true;
}

bool BudgetedContextGraph::estimate_graph(const ContextGraph& graph,
    std::size_t& bytes, std::size_t& clusters, std::size_t& retained_events,
    std::string& error) {
  bytes = 0; clusters = 0; retained_events = 0; error.clear();
  const auto& state = graph.state_;
  for (const auto& item : state.nodes) {
    if (!add(1, retained_events)) { error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false; }
    if (!add_node(item.second, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
    if (item.second.kind == NodeKind::Cluster || item.second.kind == NodeKind::ChaosCluster)
      if (!add(1, clusters)) { error = "CONTEXT_GRAPH_CLUSTER_COUNT_OVERFLOW"; return false; }
  }
  for (const auto& item : state.relations) {
    if (!add(1, retained_events)) { error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false; }
    if (!add_relation(item.second, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  for (const auto& item : state.scope_spans) {
    if (!add(1, retained_events)) { error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false; }
    AddScopeSpan span{item.second.proposal, {}};
    if (!add_scope(span, bytes)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  // State maps and append-only histories are persistent allocations too.
  for (const auto& item : state.focus_history) {
    if (!add_parts(bytes, {kEntryOverhead, item.cluster_id.size(), item.interest_id.size()}) ||
        !add(1, retained_events)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  if (state.focus && !add_parts(bytes, {kEntryOverhead, state.focus->cluster_id.size(),
                          state.focus->interest_id.size()})) {
    error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
  }
  for (const auto& item : state.question_aspect_statuses)
    if (!add_parts(bytes, {kEntryOverhead, item.first.size()}) || !add(1, retained_events)) {
      error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
    }
  for (const auto& item : state.question_memory_states)
    if (!add_parts(bytes, {kEntryOverhead, item.first.size()}) || !add(1, retained_events)) {
      error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
    }
  for (const auto& item : state.question_reference_counts) {
    if (item.second > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      error = "CONTEXT_GRAPH_EVENT_COUNT_OVERFLOW"; return false;
    }
    if (!add_parts(bytes, {kEntryOverhead, item.first.size(), sizeof(item.second)}) ||
        !add(static_cast<std::size_t>(item.second), retained_events)) {
      error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
    }
  }
  for (const auto& item : state.claim_statuses)
    if (!add_parts(bytes, {kEntryOverhead, item.first.size()}) ||
        !add(1, retained_events)) {
      error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
    }
  for (const auto& item : state.claim_validity_history) {
    if (!add_parts(bytes, {kEntryOverhead, item.first.size()})) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
    for (const auto& record : item.second)
      if (!add_parts(bytes, {kEntryOverhead, record.validity.valid_from.size(),
          record.validity.valid_until.size(), record.validity.basis_evidence_id.size(),
          record.validity.review_evidence_id.size(), record.validity.reviewer_id.size()}) ||
          !add(1, retained_events)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  for (const auto& item : state.evidence_validity_history) {
    if (!add_parts(bytes, {kEntryOverhead, item.first.size()})) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
    for (const auto& record : item.second)
      if (!add_parts(bytes, {kEntryOverhead, record.validity.valid_from.size(),
          record.validity.valid_until.size(), record.validity.basis_evidence_id.size(),
          record.validity.review_evidence_id.size(), record.validity.reviewer_id.size()}) ||
          !add(1, retained_events)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
  }
  for (const auto& item : state.dependency_evaluations) {
    if (!add_parts(bytes, {kEntryOverhead, item.first.size()}) || !add(1, retained_events)) {
      error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
    }
    for (const auto& id : item.second.impacted_nodes)
      if (!add_parts(bytes, {sizeof(std::string), id.size()})) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
    for (const auto& trigger : item.second.triggered)
      if (!add_parts(bytes, {kEntryOverhead, trigger.dependency_id.size(),
          trigger.prerequisite_id.size(), trigger.dependent_id.size()})) {
        error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
      }
  }
  for (const auto& item : state.reassessment_history) {
    if (!add_parts(bytes, {kEntryOverhead, item.first.size()})) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
    for (const auto& record : item.second) {
      if (!add_parts(bytes, {kEntryOverhead, record.trace_id.size(), record.claim_id.size(),
          record.reference_at.size(), record.decision.size(), record.previous_hash.size(),
          record.snapshot_digest.size(), record.chain_hash.size()}) ||
          !add(1, retained_events)) { error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false; }
      for (const auto& evidence : record.evidence)
        if (!add_parts(bytes, {kEntryOverhead, evidence.evidence_id.size(), evidence.relation_id.size(),
            evidence.valid_from.size(), evidence.valid_until.size(),
            evidence.basis_evidence_id.size()})) {
          error = "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"; return false;
        }
    }
  }
  return true;
}

bool BudgetedContextGraph::recover(std::string& error) {
  error.clear();
  if (recovered_) {
    error = "CONTEXT_GRAPH_BUDGET_ALREADY_RECOVERED"; return false;
  }
  std::size_t bytes = 0, clusters = 0, retained_events = 0;
  if (!estimate_graph(graph_, bytes, clusters, retained_events, error)) return false;
  ResourceLease resident, retained_clusters, retained;
  try {
    if (bytes) resident_leases_.reserve(resident_leases_.size() + 1);
    if (clusters) cluster_leases_.reserve(cluster_leases_.size() + 1);
    if (retained_events) recovery_event_leases_.reserve(recovery_event_leases_.size() + 1);
  } catch (...) {
    error = "CONTEXT_GRAPH_RECOVERY_LEASE_STAGING_FAILED";
    return false;
  }
  if (bytes && !budget_.try_acquire(ResourceKind::ResidentBytes, bytes, resident, error))
    return false;
  if (clusters && !budget_.try_acquire(ResourceKind::RetainedClusters, clusters,
                                        retained_clusters, error)) return false;
  if (retained_events && !budget_.try_acquire(ResourceKind::RetainedEvents, retained_events,
                                               retained, error)) return false;
  if (retained) recovery_event_leases_.push_back(std::move(retained));
  if (resident) resident_leases_.push_back(std::move(resident));
  if (retained_clusters) cluster_leases_.push_back(std::move(retained_clusters));
  accounted_resident_bytes_ = bytes;
  recovered_ = true;
  return true;
}

DeltaResult BudgetedContextGraph::apply(const SemanticDelta& delta) {
  if (!recovered_)
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_BUDGET_NOT_RECOVERED"};
  std::size_t bytes = 0, clusters = 0, retained_events = 0;
  std::string error;
  if (!estimate_delta(delta, bytes, clusters, retained_events, error))
    return {false, false, graph_.state_version(), error};

  // Preflight exact Entity reservations before the durable commit. Entity is
  // the only currently reclaimable kind and has no typed references of its own.
  std::size_t reclaimable_resident_bytes = 0;
  for (const auto& id : delta.reclaim_entity_node_ids) {
    const auto found = graph_.nodes().find(id);
    if (found == graph_.nodes().end() || found->second.kind != NodeKind::Entity)
      return {false, false, graph_.state_version(), "CONTEXT_GRAPH_RECLAMATION_LEASE_TARGET_INVALID"};
    if (!add(kEntryOverhead, reclaimable_resident_bytes) ||
        !add(found->second.id.size(), reclaimable_resident_bytes) ||
        !add(found->second.content.size(), reclaimable_resident_bytes))
      return {false, false, graph_.state_version(), "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"};
  }
  std::size_t available_reclaim_lease_bytes = 0;
  auto sum_leases = [&](const std::vector<ResourceLease>& leases) {
    for (const auto& lease : leases)
      if (!add(lease.amount(), available_reclaim_lease_bytes)) return false;
    return true;
  };
  if (!sum_leases(resident_leases_) || !sum_leases(mutation_resident_leases_))
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"};
  if (reclaimable_resident_bytes > available_reclaim_lease_bytes)
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_RECLAMATION_LEASE_MISSING"};

  std::vector<JournalEvent> prepared;
  ResourceLease staging, resident, retained_clusters, retained;
  // Staging is always accounted, even when no per-operation ceiling was
  // configured. A zero max_staging_bytes_ disables only that secondary ceiling;
  // it must never disable the shared transient budget.
  const auto staging_estimate = bytes > std::numeric_limits<std::size_t>::max() / 2
      ? std::numeric_limits<std::size_t>::max() : bytes * 2;
  if (max_staging_bytes_ != 0 && staging_estimate > max_staging_bytes_)
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_STAGING_BUDGET_EXCEEDED"};
  if (staging_estimate && !budget_.try_acquire(ResourceKind::TransientBytes,
                                                staging_estimate, staging, error))
    return {false, false, graph_.state_version(), error};

  // The reservation precedes event serialization/materialization below.
  if (!graph_.prepare_events(delta, prepared, error))
    return {false, false, graph_.state_version(), error};
  if (prepared.empty()) return graph_.apply(delta);

  if (bytes && !budget_.try_acquire(ResourceKind::ResidentBytes, bytes, resident, error))
    return {false, false, graph_.state_version(), error};
  if (clusters && !budget_.try_acquire(ResourceKind::RetainedClusters, clusters,
                                        retained_clusters, error))
    return {false, false, graph_.state_version(), error};
  if (retained_events && !budget_.try_acquire(ResourceKind::RetainedEvents,
                                               retained_events, retained, error))
    return {false, false, graph_.state_version(), error};

  // Reserve lease-vector capacity before the durable journal commit. Publishing
  // leases after commit then consists only of noexcept moves into existing slots.
  try {
    if (resident) resident_leases_.reserve(resident_leases_.size() + 1);
    if (retained_clusters) cluster_leases_.reserve(cluster_leases_.size() + 1);
    if (retained) recovery_event_leases_.reserve(recovery_event_leases_.size() + 1);
  } catch (...) {
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_LEASE_STAGING_FAILED"};
  }

  auto result = graph_.apply(delta);
  if (!result.committed || result.duplicate) return result;
  if (resident) resident_leases_.push_back(std::move(resident));
  if (retained_clusters) cluster_leases_.push_back(std::move(retained_clusters));
  if (retained) recovery_event_leases_.push_back(std::move(retained));
  accounted_resident_bytes_ += bytes;
  // Releasing an Entity drops only its own reservation. Shared payloads are
  // never reclaimed here; the graph-level gate has already proved zero refs.
  std::size_t remaining = reclaimable_resident_bytes;
  auto release_from = [&](std::vector<ResourceLease>& leases) {
    for (auto it = leases.rbegin(); it != leases.rend() && remaining != 0; ++it) {
      const auto take = std::min(it->amount(), remaining);
      if (take && it->shrink(take)) remaining -= take;
    }
    leases.erase(std::remove_if(leases.begin(), leases.end(),
        [](const ResourceLease& lease) { return !lease; }), leases.end());
  };
  release_from(mutation_resident_leases_);
  release_from(resident_leases_);
  if (remaining != 0) {
    // Preflight guarantees this cannot occur absent an internal accounting bug.
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_RECLAMATION_LEASE_RELEASE_INVARIANT"};
  }
  accounted_resident_bytes_ = 0;
  auto recount_resident = [&](const std::vector<ResourceLease>& leases) {
    for (const auto& lease : leases) {
      if (!add(lease.amount(), accounted_resident_bytes_)) return false;
    }
    return true;
  };
  if (!recount_resident(resident_leases_) ||
      !recount_resident(mutation_resident_leases_))
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"};
  return result;
}

DeltaResult BudgetedContextGraph::restore_cluster_archive(
    const std::string& payload, std::uint64_t expected_state_version,
    const std::string& transaction_id, const std::string& idempotency_key) {
  if (!recovered_)
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_BUDGET_NOT_RECOVERED"};
  if (payload.empty() || payload.size() >
      std::numeric_limits<std::size_t>::max() / 2)
    return {false, false, graph_.state_version(), "CLUSTER_ARCHIVE_BUDGET_SIZE_INVALID"};
  const std::size_t bytes = payload.size() * 2;
  const std::size_t staging_bytes = bytes;
  if (max_staging_bytes_ != 0 && staging_bytes > max_staging_bytes_)
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_STAGING_BUDGET_EXCEEDED"};

  std::size_t cluster_count = 0;
  std::size_t event_count = 0;
  for (const char ch : payload) if (ch == '\n') {
    if (event_count == std::numeric_limits<std::size_t>::max())
      return {false, false, graph_.state_version(), "CLUSTER_ARCHIVE_EVENT_COUNT_OVERFLOW"};
    ++event_count;
  }
  // Count only actual cluster nodes in the archive's node section.
  std::istringstream scan(payload);
  std::string line;
  for (int i = 0; i < 3; ++i)
    if (!std::getline(scan, line))
      return {false, false, graph_.state_version(), "CLUSTER_ARCHIVE_HEADER_INVALID"};
  std::size_t node_count = 0;
  {
    std::istringstream header(line);
    std::string tag, extra;
    if (!(header >> tag >> node_count) || tag != "nodes" ||
        (header >> extra) || node_count > payload.size())
      return {false, false, graph_.state_version(), "CLUSTER_ARCHIVE_NODE_COUNT_INVALID"};
  }
  for (std::size_t i = 0; i < node_count; ++i) {
    if (!std::getline(scan, line))
      return {false, false, graph_.state_version(), "CLUSTER_ARCHIVE_NODE_SECTION_INVALID"};
    std::istringstream row(line);
    std::string tag, id, content, extra;
    unsigned kind = 0;
    std::size_t refs = 0;
    if (!(row >> tag >> std::quoted(id) >> kind >> std::quoted(content) >> refs) ||
        tag != "node" || kind > static_cast<unsigned>(NodeKind::ClusterLifecycle))
      return {false, false, graph_.state_version(), "CLUSTER_ARCHIVE_NODE_SECTION_INVALID"};
    if (kind == static_cast<unsigned>(NodeKind::Cluster) ||
        kind == static_cast<unsigned>(NodeKind::ChaosCluster)) ++cluster_count;
  }
  ResourceLease staging, resident, clusters, retained;
  std::string error;
  if (staging_bytes && !budget_.try_acquire(ResourceKind::TransientBytes,
      staging_bytes, staging, error))
    return {false, false, graph_.state_version(), error};
  if (bytes && !budget_.try_acquire(ResourceKind::ResidentBytes,
      bytes, resident, error))
    return {false, false, graph_.state_version(), error};
  if (cluster_count && !budget_.try_acquire(ResourceKind::RetainedClusters,
      cluster_count, clusters, error))
    return {false, false, graph_.state_version(), error};
  if (event_count && !budget_.try_acquire(ResourceKind::RetainedEvents,
      event_count, retained, error))
    return {false, false, graph_.state_version(), error};
  try {
    if (resident) resident_leases_.reserve(resident_leases_.size() + 1);
    if (clusters) cluster_leases_.reserve(cluster_leases_.size() + 1);
    if (retained) mutation_event_leases_.reserve(mutation_event_leases_.size() + 1);
  } catch (...) {
    return {false, false, graph_.state_version(), "CLUSTER_ARCHIVE_LEASE_STAGING_FAILED"};
  }
  auto result = graph_.import_cluster_archive_payload(payload,
      expected_state_version, transaction_id, idempotency_key);
  if (!result.committed || result.duplicate) return result;
  if (resident) resident_leases_.push_back(std::move(resident));
  if (clusters) cluster_leases_.push_back(std::move(clusters));
  if (retained) mutation_event_leases_.push_back(std::move(retained));
  if (bytes > std::numeric_limits<std::size_t>::max() - accounted_resident_bytes_)
    return {false, false, graph_.state_version(), "CONTEXT_GRAPH_BUDGET_SIZE_OVERFLOW"};
  accounted_resident_bytes_ += bytes;
  return result;
}

DeltaResult BudgetedContextGraph::apply_retention_plan(
    const ClusterRetentionPlan& plan, const std::string& recorded_at,
    const std::string& reason, const std::string& provenance_digest,
    const std::string& delta_id, const std::string& idempotency_key,
    std::string& error) {
  error.clear();
  if (!plan.accepted) { error = plan.error.empty() ? "CLUSTER_RETENTION_PLAN_REJECTED" : plan.error; return {false,false,graph_.state_version(),error}; }
  if (!recovered_) { error = "CONTEXT_GRAPH_BUDGET_NOT_RECOVERED"; return {false,false,graph_.state_version(),error}; }
  if (plan.planned_state_version != graph_.state_version()) { error = "CLUSTER_RETENTION_PLAN_STALE"; return {false,false,graph_.state_version(),error}; }
  if (delta_id.empty() || idempotency_key.empty()) { error = "CLUSTER_RETENTION_DELTA_ID_INVALID"; return {false,false,graph_.state_version(),error}; }
  SemanticDelta delta; delta.delta_id=delta_id; delta.idempotency_key=idempotency_key; delta.expected_state_version=plan.planned_state_version;
  std::set<std::string> seen;
  auto append_transition = [&](const std::string& cluster_id, ClusterLifecycleState next) -> bool {
    if (!seen.insert(cluster_id).second) { error="CLUSTER_RETENTION_DUPLICATE_TARGET"; return false; }
    const auto current=graph_.cluster_lifecycle(cluster_id);
    ClusterLifecycleRecord record; record.cluster_id=cluster_id; record.state=next;
    record.revision=current?current->revision+1:1; record.recorded_at=recorded_at; record.reason=reason; record.provenance_digest=provenance_digest;
    const auto identity=sha256_hex(cluster_id); const auto node_id="cluster-lifecycle-"+identity+"-"+std::to_string(record.revision);
    const auto relation_id="cluster-lifecycle-edge-"+identity+"-"+std::to_string(record.revision);
    return graph_.append_cluster_lifecycle(delta,record,node_id,relation_id,error);
  };
  for (const auto& cluster_id:plan.forget_cluster_ids) {
    const auto current=graph_.cluster_lifecycle(cluster_id); const auto state=current?current->state:ClusterLifecycleState::Active;
    if(state==ClusterLifecycleState::Active) { if(!append_transition(cluster_id,ClusterLifecycleState::Dormant)) return {false,false,graph_.state_version(),error}; }
    else if(state==ClusterLifecycleState::Dormant) { if(!append_transition(cluster_id,ClusterLifecycleState::Archived)) return {false,false,graph_.state_version(),error}; }
  }
  for(const auto& cluster_id:plan.restore_cluster_ids) {
    const auto current=graph_.cluster_lifecycle(cluster_id);
    if(!current||(current->state!=ClusterLifecycleState::Dormant&&current->state!=ClusterLifecycleState::Archived)) { error="CLUSTER_RETENTION_RESTORE_TARGET_NOT_INACTIVE"; return {false,false,graph_.state_version(),error}; }
    if(!append_transition(cluster_id,ClusterLifecycleState::Active)) return {false,false,graph_.state_version(),error};
  }
  if(delta.nodes.empty()) return {true,true,graph_.state_version(),{}};
  return apply(delta);
}

DeltaResult BudgetedContextGraph::append_cluster_lifecycle(SemanticDelta delta,
    const ClusterLifecycleRecord& record, const std::string& lifecycle_node_id,
    const std::string& relation_id, std::string& error) {
  error.clear();
  if (!graph_.append_cluster_lifecycle(delta, record, lifecycle_node_id,
                                       relation_id, error))
    return {false, false, graph_.state_version(), error};
  auto result = apply(delta);
  if (!result.committed && !result.duplicate) error = result.error;
  return result;
}

DeltaResult BudgetedContextGraph::append_entity_reclamation(
    SemanticDelta delta,const std::string& entity_node_id,std::string& error) {
  error.clear();
  if (!graph_.append_entity_reclamation(delta, entity_node_id, error))
    return {false, false, graph_.state_version(), error};
  auto result = apply(delta);
  if (!result.committed && !result.duplicate) error = result.error;
  return result;
}

DeltaResult BudgetedContextGraph::append_cluster_unmembership(SemanticDelta delta,
    const std::string& cluster_id,const std::string& member_node_id,
    std::string& error) {
  error.clear();
  if (!graph_.append_cluster_unmembership(delta, cluster_id, member_node_id,
                                           error))
    return {false, false, graph_.state_version(), error};
  auto result = apply(delta);
  if (!result.committed && !result.duplicate) error = result.error;
  return result;
}

DeltaResult BudgetedContextGraph::append_cluster_membership(SemanticDelta delta,
    const std::string& parent_cluster_id, const std::string& classification,
    const std::string& partition_key, const std::string& member_node_id,
    const std::string& relation_prefix, std::string& assigned_cluster_id,
    std::string& error) {
  assigned_cluster_id.clear();
  error.clear();
  if (!graph_.append_cluster_membership(delta, parent_cluster_id, classification,
      partition_key, member_node_id, relation_prefix, assigned_cluster_id, error))
    return {false, false, graph_.state_version(), error};
  auto result = apply(delta);
  if (!result.committed && !result.duplicate) {
    assigned_cluster_id.clear();
    error = result.error;
  }
  return result;
}

bool BudgetedContextGraph::reserve_staging(std::size_t bytes,
    ResourceLease& staging, std::string& error) {
  if (!recovered_) {
    error = "CONTEXT_GRAPH_BUDGET_NOT_RECOVERED";
    return false;
  }
  if (max_staging_bytes_ != 0 && bytes > max_staging_bytes_) {
    error = "CONTEXT_GRAPH_STAGING_BUDGET_EXCEEDED";
    return false;
  }
  if (bytes && !budget_.try_acquire(ResourceKind::TransientBytes, bytes, staging, error))
    return false;
  return true;
}

bool BudgetedContextGraph::reserve_mutation(std::size_t bytes, std::size_t events,
    ResourceLease& resident, ResourceLease& retained, std::string& error) {
  if (!recovered_) {
    error = "CONTEXT_GRAPH_BUDGET_NOT_RECOVERED";
    return false;
  }
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
  std::size_t bytes = 512;
  if (!add(id.size(), bytes)) {
    error = "CONTEXT_GRAPH_MUTATION_SIZE_OVERFLOW"; return false;
  }
  ResourceLease staging, resident, retained;
  if (!reserve_staging(bytes, staging, error)) return false;
  if (!reserve_mutation(bytes, 1, resident, retained, error)) return false;
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
  if (!reserve_staging(bytes, staging, error)) return false;
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
  ResourceLease staging, resident, retained;
  if (!reserve_staging(512 + id.size(), staging, error)) return false;
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
  if (!reserve_staging(bytes, staging, error)) return false;
  if (!reserve_mutation(bytes, scores.size(), resident, retained, error)) return false;
  const auto before = graph_.state_version();
  if (!graph_.enforce_question_memory_capacity(scores, max_active, expected, demoted, error))
    return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

bool BudgetedContextGraph::set_question_aspect_status(const std::string& id,
    QuestionAspectStatus status, std::uint64_t expected, std::string& error) {
  std::size_t bytes = 512;
  if (!add(id.size(), bytes)) {
    error = "CONTEXT_GRAPH_MUTATION_SIZE_OVERFLOW"; return false;
  }
  ResourceLease staging, resident, retained;
  if (!reserve_staging(bytes, staging, error)) return false;
  if (!reserve_mutation(bytes, 1, resident, retained, error)) return false;
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
  if (!reserve_staging(bytes, staging, error)) return false;
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
  if (!reserve_staging(bytes, staging, error)) return false;
  if (!reserve_mutation(bytes, 1, resident, retained, error)) return false;
  const auto before = graph_.state_version();
  if (!graph_.reassess_claim(claim_id, reference_at, expected, result, error)) return false;
  if (graph_.state_version() != before) publish_mutation(std::move(resident), std::move(retained));
  return true;
}

}  // namespace surge
