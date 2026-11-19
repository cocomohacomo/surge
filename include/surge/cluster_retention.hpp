#pragma once

#include "surge/context_graph.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace surge {

// Core supplies a calibrated retention priority. This planner deliberately does
// not invent language-specific weights; importance, interest, recency and
// temporal validity are combined by the active Core policy before this input.
struct ClusterRetentionCandidate {
  std::string cluster_id;
  double retention_priority{0.0};
  std::uint64_t last_access_sequence{0};
  bool pinned{false};
};

struct ClusterRetentionPlan {
  std::vector<std::string> retained_cluster_ids;
  std::vector<std::string> forget_cluster_ids;
  std::vector<std::string> protected_cluster_ids;
  std::string error;
  bool accepted{false};
};

// Pure, deterministic planning over the canonical ContextGraph. It never
// mutates or deletes records. A later journaled compaction transaction must
// consume the plan and preserve tombstone/replay guarantees.
class ClusterRetentionPlanner {
 public:
  static ClusterRetentionPlan plan(const ContextGraph& graph,
      const std::vector<ClusterRetentionCandidate>& candidates,
      std::size_t max_retained_clusters);
};

}  // namespace surge
