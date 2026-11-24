#pragma once

#include "surge/context_graph.hpp"
#include "surge/snapshot.hpp"
#include <algorithm>
#include <iterator>
#include <set>
#include <string>

namespace surge {

struct FocusEvaluationSnapshotMetadata {
  std::uint64_t evaluated_state_version{0};
  std::string focus_digest;
  std::string observation_digest;
};

// Validates that an observation is safe to promote to a completed-evaluation
// snapshot. This is intentionally a pure contract gate; persistence/diff wiring
// is a later stage.
inline bool validate_completed_focus_evaluation(
    const CognitionObservation& observation,
    const FocusEvaluationSnapshotMetadata& metadata,
    std::string& error) {
  error.clear();
  if (!observation.error.empty()) {
    error = "FOCUS_EVALUATION_INVALIDATED";
    return false;
  }
  if (!observation.complete) {
    error = "FOCUS_EVALUATION_INCOMPLETE";
    return false;
  }
  if (metadata.focus_digest.empty() || metadata.observation_digest.empty()) {
    error = "FOCUS_EVALUATION_PROVENANCE_MISSING";
    return false;
  }
  if (metadata.evaluated_state_version != observation.state_version) {
    error = "FOCUS_EVALUATION_STATE_VERSION_MISMATCH";
    return false;
  }
  const std::set<std::string> logical(
      observation.logical_region_cluster_ids.begin(),
      observation.logical_region_cluster_ids.end());
  const std::set<std::string> processed(
      observation.processed_region_cluster_ids.begin(),
      observation.processed_region_cluster_ids.end());
  const std::set<std::string> deferred(
      observation.deferred_region_cluster_ids.begin(),
      observation.deferred_region_cluster_ids.end());
  if (logical.size() != observation.logical_region_cluster_ids.size() ||
      processed.size() != observation.processed_region_cluster_ids.size() ||
      deferred.size() != observation.deferred_region_cluster_ids.size()) {
    error = "FOCUS_EVALUATION_DUPLICATE_ROOT";
    return false;
  }
  if (!deferred.empty()) {
    error = "FOCUS_EVALUATION_DEFERRED_ROOTS";
    return false;
  }
  if (logical != processed) {
    error = "FOCUS_EVALUATION_ROOT_SET_MISMATCH";
    return false;
  }
  std::set<std::string> node_ids, relation_ids;
  for (const auto& node : observation.nodes) {
    if (node.id.empty() || !node_ids.insert(node.id).second) {
      error = "FOCUS_EVALUATION_DUPLICATE_OR_INVALID_NODE";
      return false;
    }
  }
  for (const auto& root_id : logical) {
    if (!node_ids.count(root_id)) {
      error = "FOCUS_EVALUATION_ROOT_NODE_MISSING";
      return false;
    }
  }
  for (const auto& relation : observation.relations) {
    if (relation.id.empty() || !relation_ids.insert(relation.id).second) {
      error = "FOCUS_EVALUATION_DUPLICATE_OR_INVALID_RELATION";
      return false;
    }
    if (!node_ids.count(relation.from) || !node_ids.count(relation.to)) {
      error = "FOCUS_EVALUATION_RELATION_ENDPOINT_MISSING";
      return false;
    }
  }
  return true;
}

}  // namespace surge
