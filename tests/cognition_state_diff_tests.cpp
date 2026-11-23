#include "test_support.hpp"
#include "surge/cognition_state_diff.hpp"
#include <cmath>
#include <iostream>
#include <limits>

using namespace surge;

namespace {
GraphNode snapshot_node(const std::string& payload,
    const std::string& id = "snapshot-test") {
  return {id, NodeKind::Evidence, "SURGE_COGNITION_STATE_V1 " + payload, {}};
}
}

int main() {
  GraphNode old_node{"snapshot-old", NodeKind::Evidence,
      "SURGE_COGNITION_STATE_V1 \"cluster-a\" \"input-a\" 4 0.25 2 \"e1\" \"e2\" 2 \"river\" \"habitat\"", {}};
  GraphNode new_node{"snapshot-new", NodeKind::Evidence,
      "SURGE_COGNITION_STATE_V1 \"cluster-a\" \"input-a\" 5 0.5 2 \"e2\" \"e3\" 2 \"habitat\" \"floodplain\"", {}};
  CognitionStateSnapshot old_state, new_state;
  std::string error;
  CHECK(decode_cognition_state_snapshot(old_node, old_state, error));
  CHECK(decode_cognition_state_snapshot(new_node, new_state, error));
  CHECK(old_state.snapshot_id == "snapshot-old");
  CHECK(old_state.evaluated_state_version == 4);

  CognitionStateDiff diff;
  CHECK(diff_cognition_state_snapshots(old_state, new_state, diff, error));
  CHECK(diff.added_supporting_member_ids.size() == 1);
  CHECK(diff.added_supporting_member_ids.front() == "e3");
  CHECK(diff.retained_supporting_member_ids.size() == 1);
  CHECK(diff.retained_supporting_member_ids.front() == "e2");
  CHECK(diff.removed_supporting_member_ids.size() == 1);
  CHECK(diff.removed_supporting_member_ids.front() == "e1");
  CHECK(diff.added_matched_terms.size() == 1);
  CHECK(diff.added_matched_terms.front() == "floodplain");
  CHECK(diff.retained_matched_terms.size() == 1);
  CHECK(diff.retained_matched_terms.front() == "habitat");
  CHECK(diff.removed_matched_terms.size() == 1);
  CHECK(diff.removed_matched_terms.front() == "river");
  CHECK(std::abs(diff.recall_score_delta - 0.25) < 1e-12);

  // Repeated comparison is deterministic and does not mutate either snapshot.
  CognitionStateDiff repeated;
  CHECK(diff_cognition_state_snapshots(old_state, new_state, repeated, error));
  CHECK(repeated.added_supporting_member_ids == diff.added_supporting_member_ids);
  CHECK(repeated.retained_supporting_member_ids == diff.retained_supporting_member_ids);
  CHECK(repeated.removed_supporting_member_ids == diff.removed_supporting_member_ids);
  CHECK(repeated.added_matched_terms == diff.added_matched_terms);
  CHECK(repeated.retained_matched_terms == diff.retained_matched_terms);
  CHECK(repeated.removed_matched_terms == diff.removed_matched_terms);
  CHECK(repeated.recall_score_delta == diff.recall_score_delta);

  auto trailing_whitespace = old_node;
  trailing_whitespace.content += "  \t\n";
  CognitionStateSnapshot whitespace_state;
  CHECK(decode_cognition_state_snapshot(trailing_whitespace, whitespace_state, error));

  auto malformed = old_node;
  malformed.content += " trailing";
  CHECK(!decode_cognition_state_snapshot(malformed, old_state, error));
  CHECK(!diff_cognition_state_snapshots(new_state, old_state, diff, error));

  auto wrong_kind = old_node;
  wrong_kind.kind = NodeKind::Claim;
  CHECK(!decode_cognition_state_snapshot(wrong_kind, old_state, error));

  CHECK(!decode_cognition_state_snapshot(
      snapshot_node("\"cluster-a\" \"input-a\" 4 0.25 2 \"e1\" \"e1\" 0"), old_state, error));
  CHECK(!decode_cognition_state_snapshot(
      snapshot_node("\"cluster-a\" \"input-a\" 4 0.25 0 2 \"same\" \"same\""), old_state, error));
  CHECK(!decode_cognition_state_snapshot(
      snapshot_node("\"cluster-a\" \"input-a\" 4 nan 0 0"), old_state, error));
  CHECK(!decode_cognition_state_snapshot(
      snapshot_node("\"cluster-a\" \"input-a\" 4 inf 0 0"), old_state, error));
  CHECK(!decode_cognition_state_snapshot(
      snapshot_node("\"cluster-a\" \"input-a\" 4 -0.1 0 0"), old_state, error));
  CHECK(!decode_cognition_state_snapshot(
      snapshot_node("\"cluster-a\" \"input-a\" 4 0.1 100001 0"), old_state, error));
  CHECK(!decode_cognition_state_snapshot(
      snapshot_node("\"cluster-a\" \"input-a\" 4 0.1 1 \"\" 0"), old_state, error));
  CHECK(!decode_cognition_state_snapshot(
      snapshot_node("\"cluster-a\" \"input-a\" 4 0.1 0 1 \"unterminated"), old_state, error));

  auto other_target = new_state;
  other_target.cluster_id = "cluster-b";
  CHECK(!diff_cognition_state_snapshots(old_state, other_target, diff, error));

  auto direct_duplicate = new_state;
  direct_duplicate.supporting_member_ids.push_back("e3");
  CHECK(!diff_cognition_state_snapshots(old_state, direct_duplicate, diff, error));
  auto direct_empty = new_state;
  direct_empty.matched_terms.push_back("");
  CHECK(!diff_cognition_state_snapshots(old_state, direct_empty, diff, error));
  auto direct_invalid_score = new_state;
  direct_invalid_score.recall_score = std::numeric_limits<double>::infinity();
  CHECK(!diff_cognition_state_snapshots(old_state, direct_invalid_score, diff, error));

  std::cout << "SURGE cognition state decode/diff passed\\n";
  return 0;
}
