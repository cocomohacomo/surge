#include "surge/decision_c_api.h"
#include <cassert>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

int main() {
  assert(surge_decision_c_abi_version() == SURGE_DECISION_C_ABI_VERSION);
  const SurgeDecisionEvidenceInput surface_evidence[] = {
    {"kanji-evidence", "orthography", "kanji", 0.2, 1.0, 1},
    {"kana-evidence", "orthography", "kana", 1.0, 1.0, 1}
  };
  const SurgeDecisionHypothesisInput hypotheses[] = {
    {"old-kanji", 0, "旧字", 0, 1},
    {"old-kana", 1, "きゅうじ", 1, 1},
    {"kanji", 0, "漢字", 0, 1},
    {"kana", 1, "かな", 1, 1}
  };
  const SurgeDecisionSurfaceSetInput sets[] = {
    {"span:0:2", 0, 2}, {"span:0:2", 2, 2}
  };
  const SurgeDecisionEvidenceInput two_candidate_evidence[] = {
    {"weak", "lexical", "weak", 0.5, 1.0, 1},
    {"strong", "lexical", "strong", 1.0, 1.0, 1}
  };
  const SurgeDecisionCandidateInput candidates[] = {
    {"candidate-0", 0, 0, 1, 0, 1},
    {"candidate-1", 1, 1, 1, 1, 1}
  };
  size_t selected_indices[] = {
    std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()
  };
  int disposition = -1;
  char selected_id[64] = {};
  char reason[128] = {};
  double score = 0.0, margin = 0.0;
  assert(surge_unified_decision_evaluate(
      candidates, 2, two_candidate_evidence, 2, sets, 2, hypotheses, 4,
      surface_evidence, 2, nullptr, 0, nullptr, &disposition,
      selected_id, sizeof(selected_id), reason, sizeof(reason), &score, &margin,
      selected_indices, 2) == 1);
  assert(disposition == 0);
  assert(selected_id[0] != '\0');
  assert(selected_indices[0] == std::numeric_limits<size_t>::max());
  assert(selected_indices[1] == 3);

  SurgeDecisionPolicyInput invalid_policy{};
  invalid_policy.minimum_margin = -1.0;
  assert(surge_unified_decision_evaluate(
      candidates, 2, two_candidate_evidence, 2, sets, 2, hypotheses, 4,
      surface_evidence, 2, nullptr, 0, &invalid_policy, &disposition,
      selected_id, sizeof(selected_id), reason, sizeof(reason), &score, &margin,
      selected_indices, 2) == 1);
  assert(disposition == 2);

  // Repeated flat-array references must be bounded before nested evidence copies.
  std::vector<SurgeDecisionEvidenceInput> expanded_evidence;
  expanded_evidence.reserve(16385);
  for (std::size_t i = 0; i < 16385; ++i)
    expanded_evidence.push_back({"e", "family", "group", 1.0, 1.0, 1});
  const SurgeDecisionCandidateInput expanded_candidate[] = {
    {"expanded", 0, 0, 1, 0, 1}
  };
  const SurgeDecisionSurfaceSetInput expanded_set[] = {{"span", 0, 1}};
  const SurgeDecisionHypothesisInput expanded_hypothesis[] = {
    {"surface", 0, "表層", 0, expanded_evidence.size()}
  };
  const SurgeDecisionEvidenceInput candidate_evidence[] = {
    {"candidate-evidence", "lexical", "candidate", 1.0, 1.0, 1}
  };
  size_t one_selected_index = std::numeric_limits<size_t>::max();
  assert(surge_unified_decision_evaluate(
      expanded_candidate, 1, candidate_evidence, 1,
      expanded_set, 1, expanded_hypothesis, 1,
      expanded_evidence.data(), expanded_evidence.size(), nullptr, 0, nullptr,
      &disposition, selected_id, sizeof(selected_id), reason, sizeof(reason),
      &score, &margin, &one_selected_index, 1) == 1);
  assert(disposition == 1);
  assert(std::string(reason) == "UNIFIED_SURFACE_EVIDENCE_BUDGET_EXCEEDED");
  assert(one_selected_index == std::numeric_limits<size_t>::max());
  return 0;
}
