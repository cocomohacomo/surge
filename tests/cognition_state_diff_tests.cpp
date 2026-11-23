#include "test_support.hpp"
#include "surge/cognition_state_diff.hpp"
#include <cmath>
#include <iostream>

using namespace surge;

int main() {
  GraphNode old_node{"snapshot-old", NodeKind::Evidence,
      "SURGE_COGNITION_STATE_V1 \"cluster-a\" \"input-a\" 4 0.25 2 \"e1\" \"e2\" 2 \"river\" \"habitat\"", {}};
  GraphNode new_node{"snapshot-new", NodeKind::Evidence,
      "SURGE_COGNITION_STATE_V1 \"cluster-a\" \"input-a\" 5 0.5 2 \"e2\" \"e3\" 2 \"habitat\" \"floodplain\"", {}};
  CognitionStateSnapshot old_state, new_state;
  std::string error;
  CHECK(decode_cognition_state_snapshot(old_node, old_state, error));
  CHECK(decode_cognition_state_snapshot(new_node, new_state, error));

  CognitionStateDiff diff;
  CHECK(diff_cognition_state_snapshots(old_state, new_state, diff, error));
  CHECK(diff.added_supporting_member_ids.size() == 1);
  CHECK(diff.added_supporting_member_ids.front() == "e3");
  CHECK(diff.retained_supporting_member_ids.size() == 1);
  CHECK(diff.retained_supporting_member_ids.front() == "e2");
  CHECK(diff.removed_supporting_member_ids.size() == 1);
  CHECK(diff.removed_supporting_member_ids.front() == "e1");
  CHECK(diff.added_matched_terms.size() == 1);
  CHECK(diff.removed_matched_terms.size() == 1);
  CHECK(std::abs(diff.recall_score_delta - 0.25) < 1e-12);

  auto malformed = old_node;
  malformed.content += " trailing";
  CHECK(!decode_cognition_state_snapshot(malformed, old_state, error));
  CHECK(!diff_cognition_state_snapshots(new_state, old_state, diff, error));

  auto other_target = new_state;
  other_target.cluster_id = "cluster-b";
  CHECK(!diff_cognition_state_snapshots(old_state, other_target, diff, error));
  std::cout << "SURGE cognition state decode/diff passed\\n";
  return 0;
}
