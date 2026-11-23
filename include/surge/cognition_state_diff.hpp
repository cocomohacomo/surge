#pragma once
#include "surge/context_graph.hpp"
#include <string>
#include <vector>

namespace surge {

struct CognitionStateDiff {
  std::string cluster_id;
  std::string input_member_id;
  std::uint64_t previous_state_version{0};
  std::uint64_t current_state_version{0};
  double recall_score_delta{0.0};
  std::vector<std::string> added_supporting_member_ids;
  std::vector<std::string> retained_supporting_member_ids;
  std::vector<std::string> removed_supporting_member_ids;
  std::vector<std::string> added_matched_terms;
  std::vector<std::string> retained_matched_terms;
  std::vector<std::string> removed_matched_terms;
};

// Decodes an immutable cognition snapshot Evidence node. No graph mutation.
bool decode_cognition_state_snapshot(const GraphNode& node,
    CognitionStateSnapshot& snapshot, std::string& error);

// Compares two decoded snapshots of the same cognition target. "Removed" means
// absent from the later snapshot only; it is not a truth, retraction, or deletion judgment.
bool diff_cognition_state_snapshots(const CognitionStateSnapshot& previous,
    const CognitionStateSnapshot& current, CognitionStateDiff& diff,
    std::string& error);

} // namespace surge
