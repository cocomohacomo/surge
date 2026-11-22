#pragma once

#include "surge/context_graph.hpp"
#include "surge/packet_admission.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace surge {

struct ProjectionMembership {
  std::string relation_id;
  std::string member_ref;
  std::string cluster_ref;
  double upstream_weight{1.0};
};

struct ProjectionBatch {
  std::string projection_id;
  std::string idempotency_key;
  std::string context_id;
  std::string producer_id;
  std::string producer_version;
  std::string policy_version;
  std::uint64_t base_state_version{0};
  std::vector<std::string> source_event_refs;
  std::vector<std::string> evidence_refs;
  std::vector<GraphNode> nodes;
  std::vector<GraphRelation> relations;
  std::vector<ProjectionMembership> memberships;
  std::vector<ClusterSpatialPosition> spatial_placements;
};

struct ProjectionValidationResult {
  bool valid{false};
  std::string error;
};

inline ProjectionValidationResult validate_projection_batch(const ProjectionBatch& batch) {
  const auto required = [](const std::string& value) { return !value.empty(); };
  if (!required(batch.projection_id) || !required(batch.idempotency_key) ||
      !required(batch.context_id) || !required(batch.producer_id) ||
      !required(batch.producer_version) || !required(batch.policy_version))
    return {false, "MISSING_REQUIRED_METADATA"};
  if (batch.source_event_refs.empty() || batch.evidence_refs.empty())
    return {false, "MISSING_SOURCE_OR_EVIDENCE"};

  std::set<std::string> source_event_ids;
  for (const auto& event_id : batch.source_event_refs) {
    if (!required(event_id) || !source_event_ids.insert(event_id).second)
      return {false, "EMPTY_OR_DUPLICATE_SOURCE_EVENT_REF"};
  }

  std::set<std::string> node_ids;
  for (const auto& node : batch.nodes) {
    if (!required(node.id) || !node_ids.insert(node.id).second)
      return {false, "EMPTY_OR_DUPLICATE_NODE_ID"};
    for (const auto& reference : node.references)
      if (!required(reference)) return {false, "EMPTY_NODE_REFERENCE"};
  }
  std::set<std::string> evidence_ids;
  for (const auto& evidence_id : batch.evidence_refs) {
    if (!required(evidence_id) || !evidence_ids.insert(evidence_id).second)
      return {false, "EMPTY_OR_DUPLICATE_EVIDENCE_REF"};
    const auto found = std::find_if(batch.nodes.begin(), batch.nodes.end(),
      [&](const GraphNode& node) { return node.id == evidence_id && node.kind == NodeKind::Evidence; });
    if (found == batch.nodes.end()) return {false, "EVIDENCE_NODE_NOT_FOUND"};
  }

  std::set<std::string> relation_ids;
  for (const auto& relation : batch.relations) {
    if (!required(relation.id) || !relation_ids.insert(relation.id).second)
      return {false, "EMPTY_OR_DUPLICATE_RELATION_ID"};
    if (!std::isfinite(relation.weight)) return {false, "NON_FINITE_RELATION_WEIGHT"};
  }
  for (const auto& membership : batch.memberships) {
    if (!required(membership.relation_id) || !relation_ids.insert(membership.relation_id).second)
      return {false, "EMPTY_OR_DUPLICATE_MEMBERSHIP_ID"};
    if (!required(membership.member_ref) || !required(membership.cluster_ref))
      return {false, "MISSING_MEMBERSHIP_ENDPOINT"};
    if (!std::isfinite(membership.upstream_weight))
      return {false, "NON_FINITE_MEMBERSHIP_WEIGHT"};
  }
  std::set<std::string> placed_cluster_ids;
  for (const auto& placement : batch.spatial_placements) {
    if (!required(placement.cluster_id) ||
        !placed_cluster_ids.insert(placement.cluster_id).second ||
        !std::isfinite(placement.position.x) ||
        !std::isfinite(placement.position.y) ||
        !std::isfinite(placement.position.z))
      return {false, "INVALID_OR_DUPLICATE_SPATIAL_PLACEMENT"};
  }
  return {true, {}};
}

// Translate a validated, transport-neutral batch into the existing atomic
// SemanticDelta path. The caller still applies it through ContextGraph::apply.
inline bool build_projection_delta(const ProjectionBatch& batch,
                                   SemanticDelta& delta,
                                   std::string& error) {
  const auto validation = validate_projection_batch(batch);
  if (!validation.valid) { error = validation.error; return false; }

  delta = SemanticDelta{};
  delta.delta_id = batch.projection_id;
  delta.idempotency_key = batch.idempotency_key;
  delta.expected_state_version = batch.base_state_version;

  // ContextGraph applies nodes in order and requires in-batch references to
  // exist already. Preserve projector order where possible while emitting
  // referenced nodes before their dependents.
  std::set<std::string> pending_node_ids;
  for (const auto& node : batch.nodes) pending_node_ids.insert(node.id);
  while (!pending_node_ids.empty()) {
    bool progressed = false;
    for (const auto& node : batch.nodes) {
      if (!pending_node_ids.count(node.id)) continue;
      const bool dependencies_ready = std::none_of(
          node.references.begin(), node.references.end(),
          [&](const std::string& reference) { return pending_node_ids.count(reference) != 0; });
      if (!dependencies_ready) continue;
      delta.nodes.push_back({node});
      pending_node_ids.erase(node.id);
      progressed = true;
    }
    if (!progressed) {
      error = "NODE_REFERENCE_CYCLE";
      delta = SemanticDelta{};
      return false;
    }
  }
  for (const auto& relation : batch.relations) delta.relations.push_back({relation});
  for (const auto& membership : batch.memberships) {
    GraphRelation relation;
    relation.id = membership.relation_id;
    relation.kind = RelationKind::Contains;
    relation.from = membership.cluster_ref;
    relation.to = membership.member_ref;
    relation.weight = membership.upstream_weight;
    delta.relations.push_back({relation});
  }
  for (const auto& placement : batch.spatial_placements)
    delta.cluster_position_updates.push_back({placement});
  error.clear();
  return true;
}

// Direct graph-only commit for callers whose source Event has already been admitted.
inline DeltaResult apply_projection_batch(ContextGraph& graph, const ProjectionBatch& batch) {
  SemanticDelta delta;
  std::string error;
  if (!build_projection_delta(batch, delta, error))
    return {false, false, graph.state_version(), error};
  return graph.apply(delta);
}

// Atomic source-packet + projection commit. Packet admission and the prepared
// SemanticDelta are written by PacketAdmissionCoordinator in one EventJournal
// transaction. The source packet is the single source event represented here;
// multi-event projections are submitted as successive source packets.
inline PacketAdmissionResult apply_projection_batch(
    PacketAdmissionCoordinator& packets, ContextGraph& graph,
    const ProjectionBatch& batch, const ValidatedRecognitionPacket& packet,
    const std::string& normalized_source_snapshot) {
  PacketAdmissionResult rejected;
  rejected.state_version = graph.state_version();
  const auto validation = validate_projection_batch(batch);
  if (!validation.valid) {
    rejected.error = validation.error;
    return rejected;
  }
  if (batch.context_id != packet.context_id || batch.source_event_refs.size() != 1 ||
      batch.source_event_refs.front() != packet.packet_id) {
    rejected.error = "PROJECTION_SOURCE_PACKET_MISMATCH";
    return rejected;
  }

  SemanticDelta delta;
  std::string error;
  if (!build_projection_delta(batch, delta, error)) {
    rejected.error = error;
    return rejected;
  }
  std::vector<JournalEvent> semantic_events;
  if (!graph.prepare_events(delta, semantic_events, error)) {
    rejected.error = error;
    return rejected;
  }
  if (semantic_events.empty() && !packets.contains_packet(packet.packet_id)) {
    rejected.error = "PROJECTION_IDEMPOTENCY_KEY_REUSED";
    return rejected;
  }
  auto admitted = packets.admit(packet, normalized_source_snapshot, semantic_events);
  if (admitted.admitted && !graph.open()) {
    admitted.admitted = false;
    admitted.error = "PROJECTION_COMMIT_SUCCEEDED_GRAPH_REPLAY_FAILED";
  }
  return admitted;
}

} // namespace surge
