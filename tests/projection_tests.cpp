#include "surge/projection.hpp"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
  surge::ProjectionBatch batch;
  batch.projection_id = "projection-1";
  batch.idempotency_key = "projection-1-v1";
  batch.context_id = "context-1";
  batch.producer_id = "recognizer";
  batch.producer_version = "1.0";
  batch.policy_version = "ja-v1";
  batch.base_state_version = 7;
  batch.source_event_refs = {"event-1"};
  batch.evidence_refs = {"evidence-1"};
  batch.nodes = {
    {"cluster-1", surge::NodeKind::Cluster, "topic", {}},
    {"claim-1", surge::NodeKind::Claim, "meeting is Tuesday", {"evidence-1"}},
    {"evidence-1", surge::NodeKind::Evidence, "source span", {"event-1"}}
  };
  batch.memberships.push_back({"membership-1", "claim-1", "cluster-1", 0.82});
  batch.spatial_placements.push_back({"cluster-1", {{1.0, 2.0, 3.0}}});

  auto validation = surge::validate_projection_batch(batch);
  assert(validation.valid);

  surge::SemanticDelta delta;
  std::string error;
  assert(surge::build_projection_delta(batch, delta, error));
  assert(delta.delta_id == "projection-1");
  assert(delta.expected_state_version == 7);
  assert(delta.nodes.size() == 3);
  assert(delta.relations.size() == 1);
  assert(delta.relations.front().relation.kind == surge::RelationKind::Contains);
  assert(std::abs(delta.relations.front().relation.weight - 0.82) < 1e-12);
  assert(delta.cluster_position_updates.size() == 1);

  auto invalid = batch;
  invalid.memberships.front().upstream_weight = std::numeric_limits<double>::quiet_NaN();
  validation = surge::validate_projection_batch(invalid);
  assert(!validation.valid && validation.error == "NON_FINITE_MEMBERSHIP_WEIGHT");

  invalid = batch;
  invalid.evidence_refs = {"missing-evidence"};
  validation = surge::validate_projection_batch(invalid);
  assert(!validation.valid && validation.error == "EVIDENCE_NODE_NOT_FOUND");

  invalid = batch;
  invalid.spatial_placements.front().position.x = std::numeric_limits<double>::infinity();
  validation = surge::validate_projection_batch(invalid);
  assert(!validation.valid && validation.error == "INVALID_SPATIAL_PLACEMENT");

  invalid = batch;
  invalid.idempotency_key.clear();
  validation = surge::validate_projection_batch(invalid);
  assert(!validation.valid && validation.error == "MISSING_REQUIRED_METADATA");
  return 0;
}
