#include "surge/cognition_state_diff.hpp"
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace surge {
namespace {
constexpr const char* kPrefix = "SURGE_COGNITION_STATE_V1 ";
constexpr std::size_t kMaxItems = 100000;

bool read_unique_items(std::istringstream& in, std::vector<std::string>& values) {
  std::size_t count = 0;
  if (!(in >> count) || count > kMaxItems) return false;
  std::set<std::string> seen;
  values.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::string value;
    if (!(in >> std::quoted(value)) || value.empty() || !seen.insert(value).second)
      return false;
    values.push_back(std::move(value));
  }
  return true;
}

void compare_sets(const std::vector<std::string>& before,
    const std::vector<std::string>& after, std::vector<std::string>& added,
    std::vector<std::string>& retained, std::vector<std::string>& removed) {
  const std::set<std::string> a(before.begin(), before.end());
  const std::set<std::string> b(after.begin(), after.end());
  for (const auto& value : after)
    (a.count(value) ? retained : added).push_back(value);
  for (const auto& value : before)
    if (!b.count(value)) removed.push_back(value);
}
} // namespace

bool decode_cognition_state_snapshot(const GraphNode& node,
    CognitionStateSnapshot& snapshot, std::string& error) {
  if (node.kind != NodeKind::Evidence || node.id.empty() ||
      node.content.rfind(kPrefix, 0) != 0) {
    error = "node is not a cognition state snapshot Evidence";
    return false;
  }
  std::istringstream in(node.content.substr(std::char_traits<char>::length(kPrefix)));
  CognitionStateSnapshot decoded;
  decoded.snapshot_id = node.id;
  if (!(in >> std::quoted(decoded.cluster_id) >>
        std::quoted(decoded.input_member_id) >> decoded.evaluated_state_version >>
        decoded.recall_score) ||
      decoded.cluster_id.empty() || decoded.input_member_id.empty() ||
      !std::isfinite(decoded.recall_score) || decoded.recall_score < 0.0 ||
      !read_unique_items(in, decoded.supporting_member_ids) ||
      !read_unique_items(in, decoded.matched_terms)) {
    error = "malformed cognition state snapshot payload";
    return false;
  }
  in >> std::ws;
  if (!in.eof()) {
    error = "trailing data in cognition state snapshot payload";
    return false;
  }
  snapshot = std::move(decoded);
  error.clear();
  return true;
}

bool diff_cognition_state_snapshots(const CognitionStateSnapshot& previous,
    const CognitionStateSnapshot& current, CognitionStateDiff& diff,
    std::string& error) {
  if (previous.cluster_id.empty() || previous.input_member_id.empty() ||
      current.cluster_id.empty() || current.input_member_id.empty() ||
      previous.cluster_id != current.cluster_id ||
      previous.input_member_id != current.input_member_id) {
    error = "cognition snapshots refer to different cognition targets";
    return false;
  }
  if (current.evaluated_state_version < previous.evaluated_state_version) {
    error = "cognition snapshot state version regressed";
    return false;
  }
  if (!std::isfinite(previous.recall_score) || previous.recall_score < 0.0 ||
      !std::isfinite(current.recall_score) || current.recall_score < 0.0) {
    error = "cognition snapshot score is invalid";
    return false;
  }
  CognitionStateDiff result;
  result.cluster_id = current.cluster_id;
  result.input_member_id = current.input_member_id;
  result.previous_state_version = previous.evaluated_state_version;
  result.current_state_version = current.evaluated_state_version;
  result.recall_score_delta = current.recall_score - previous.recall_score;
  compare_sets(previous.supporting_member_ids, current.supporting_member_ids,
      result.added_supporting_member_ids, result.retained_supporting_member_ids,
      result.removed_supporting_member_ids);
  compare_sets(previous.matched_terms, current.matched_terms,
      result.added_matched_terms, result.retained_matched_terms,
      result.removed_matched_terms);
  diff = std::move(result);
  error.clear();
  return true;
}

} // namespace surge
