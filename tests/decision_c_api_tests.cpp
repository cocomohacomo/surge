#include "surge/decision_c_api.h"
#include <cassert>
#include <cstddef>
#include <limits>

int main() {
  assert(surge_decision_c_abi_version() == SURGE_DECISION_C_ABI_VERSION);
  const SurgeDecisionEvidenceInput candidate_evidence[] = {
    {"recognition", "lexical", "token", 1.0, 1.0, 1}
  };
  const SurgeDecisionEvidenceInput surface_evidence[] = {
    {"kanji-evidence", "orthography", "kanji", 0.2, 1.0, 1},
    {"kana-evidence", "orthography", "kana", 1.0, 1.0, 1}
  };
  const SurgeDecisionHypothesisInput hypotheses[] = {
    {"kanji", 0, "漢字", 0, 1},
    {"kana", 1, "かな", 1, 1}
  };
  const SurgeDecisionSurfaceSetInput sets[] = {{"span:0:2", 0, 2}};
  const SurgeDecisionCandidateInput candidates[] = {
    {"candidate-1", 0, 0, 1, 0, 1}
  };
  size_t selected_indices[] = {std::numeric_limits<size_t>::max()};
  int disposition = -1;
  char selected_id[64] = {};
  char reason[128] = {};
  double score = 0.0, margin = 0.0;
  assert(surge_unified_decision_evaluate(
      candidates, 1, candidate_evidence, 1, sets, 1, hypotheses, 2,
      surface_evidence, 2, nullptr, 0, nullptr, &disposition,
      selected_id, sizeof(selected_id), reason, sizeof(reason), &score, &margin,
      selected_indices, 1) == 1);
  assert(disposition == 0);
  assert(selected_id[0] != '\0');
  assert(selected_indices[0] == 1);

  SurgeDecisionPolicyInput invalid_policy{};
  invalid_policy.minimum_margin = -1.0;
  assert(surge_unified_decision_evaluate(
      candidates, 1, candidate_evidence, 1, sets, 1, hypotheses, 2,
      surface_evidence, 2, nullptr, 0, &invalid_policy, &disposition,
      selected_id, sizeof(selected_id), reason, sizeof(reason), &score, &margin,
      selected_indices, 1) == 1);
  assert(disposition == 2);
  return 0;
}
