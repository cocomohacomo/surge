#include "surge/decision_c_api.h"
#include "surge/decision.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
void copy_text(char* out, size_t capacity, const std::string& value) {
  if (!out || capacity == 0) return;
  const size_t n = std::min(capacity - 1, value.size());
  std::memcpy(out, value.data(), n);
  out[n] = '\0';
}
bool range_ok(size_t offset, size_t count, size_t total) {
  return offset <= total && count <= total - offset;
}
bool required(const char* value) { return value != nullptr && value[0] != '\0'; }
surge::DecisionEvidence convert(const SurgeDecisionEvidenceInput& input) {
  return {input.evidence_id ? input.evidence_id : "",
          input.family ? input.family : "",
          input.correlation_group ? input.correlation_group : "",
          input.value, input.weight, input.valid != 0};
}
}

extern "C" {
unsigned int surge_decision_c_abi_version(void) {
  return SURGE_DECISION_C_ABI_VERSION;
}

int surge_unified_decision_evaluate(
    const SurgeDecisionCandidateInput* candidates, size_t candidate_count,
    const SurgeDecisionEvidenceInput* evidence, size_t evidence_count,
    const SurgeDecisionSurfaceSetInput* surface_sets, size_t surface_set_count,
    const SurgeDecisionHypothesisInput* hypotheses, size_t hypothesis_count,
    const SurgeDecisionEvidenceInput* surface_evidence, size_t surface_evidence_count,
    const SurgeDecisionFamilyWeightInput* family_weights, size_t family_weight_count,
    const SurgeDecisionPolicyInput* policy,
    int* disposition, char* selected_candidate_id, size_t selected_id_capacity,
    char* reason, size_t reason_capacity, double* selected_score, double* margin,
    size_t* selected_hypothesis_indices, size_t selected_indices_count) {
  copy_text(selected_candidate_id, selected_id_capacity, "");
  copy_text(reason, reason_capacity, "C_API_ARGUMENT_INVALID");
  if (disposition) *disposition = static_cast<int>(surge::DecisionDisposition::Reject);
  if (selected_score) *selected_score = 0.0;
  if (margin) *margin = 0.0;
  if (selected_hypothesis_indices)
    std::fill(selected_hypothesis_indices,
              selected_hypothesis_indices + selected_indices_count,
              std::numeric_limits<size_t>::max());
  if (!disposition || !selected_candidate_id || selected_id_capacity == 0 ||
      !reason || reason_capacity == 0 || !selected_score || !margin ||
      (!candidates && candidate_count) || (!evidence && evidence_count) ||
      (!surface_sets && surface_set_count) || (!hypotheses && hypothesis_count) ||
      (!surface_evidence && surface_evidence_count) ||
      (!family_weights && family_weight_count) ||
      (!selected_hypothesis_indices && selected_indices_count) ||
      selected_indices_count != surface_set_count ||
      candidate_count > 4096 || evidence_count > 65536 ||
      surface_set_count > 4096 || hypothesis_count > 65536 ||
      surface_evidence_count > 65536 || family_weight_count > 256) return 0;
  try {
    surge::DecisionPolicy core_policy;
    if (policy) {
      core_policy.max_candidates = policy->max_candidates;
      core_policy.max_evidence_per_candidate = policy->max_evidence_per_candidate;
      core_policy.max_family_weights = policy->max_family_weights;
      core_policy.max_surface_sets = policy->max_surface_sets;
      core_policy.max_surface_hypotheses_total = policy->max_surface_hypotheses_total;
      core_policy.max_surface_trace_entries = policy->max_surface_trace_entries;
      core_policy.max_source_ref_bytes = policy->max_source_ref_bytes;
      core_policy.max_surface_bytes = policy->max_surface_bytes;
      core_policy.minimum_score = policy->minimum_score;
      core_policy.minimum_margin = policy->minimum_margin;
      core_policy.require_valid_evidence = policy->require_valid_evidence != 0;
    }
    for (size_t i = 0; i < family_weight_count; ++i) {
      if (!required(family_weights[i].family)) return 0;
      if (!core_policy.family_weights.emplace(family_weights[i].family,
                                               family_weights[i].weight).second) return 0;
    }
    std::vector<surge::DecisionEvidence> converted_evidence;
    converted_evidence.reserve(evidence_count);
    for (size_t i = 0; i < evidence_count; ++i) converted_evidence.push_back(convert(evidence[i]));
    std::vector<surge::DecisionEvidence> converted_surface_evidence;
    converted_surface_evidence.reserve(surface_evidence_count);
    for (size_t i = 0; i < surface_evidence_count; ++i)
      converted_surface_evidence.push_back(convert(surface_evidence[i]));
    std::vector<surge::DecisionCandidate> converted_candidates;
    converted_candidates.reserve(candidate_count);
    for (size_t i = 0; i < candidate_count; ++i) {
      const auto& input = candidates[i];
      if (!required(input.candidate_id) ||
          !range_ok(input.evidence_offset, input.evidence_count, evidence_count) ||
          !range_ok(input.surface_set_offset, input.surface_set_count, surface_set_count)) return 0;
      surge::DecisionCandidate candidate;
      candidate.candidate_id = input.candidate_id;
      candidate.stable_order = input.stable_order;
      candidate.evidence.assign(converted_evidence.begin() + input.evidence_offset,
                                converted_evidence.begin() + input.evidence_offset + input.evidence_count);
      for (size_t j = 0; j < input.surface_set_count; ++j) {
        const auto& set_input = surface_sets[input.surface_set_offset + j];
        if (!required(set_input.source_ref) ||
            !range_ok(set_input.hypothesis_offset, set_input.hypothesis_count, hypothesis_count)) return 0;
        surge::DecisionSurfaceSet set;
        set.source_ref = set_input.source_ref;
        for (size_t k = 0; k < set_input.hypothesis_count; ++k) {
          const auto& hypothesis_input = hypotheses[set_input.hypothesis_offset + k];
          if (!required(hypothesis_input.hypothesis_id) || !hypothesis_input.surface ||
              !range_ok(hypothesis_input.evidence_offset, hypothesis_input.evidence_count,
                        surface_evidence_count)) return 0;
          surge::DecisionSurfaceHypothesis hypothesis;
          hypothesis.hypothesis_id = hypothesis_input.hypothesis_id;
          hypothesis.stable_order = hypothesis_input.stable_order;
          hypothesis.surface = hypothesis_input.surface;
          hypothesis.evidence.assign(
              converted_surface_evidence.begin() + hypothesis_input.evidence_offset,
              converted_surface_evidence.begin() + hypothesis_input.evidence_offset +
                  hypothesis_input.evidence_count);
          set.hypotheses.push_back(std::move(hypothesis));
        }
        candidate.surface_sets.push_back(std::move(set));
      }
      converted_candidates.push_back(std::move(candidate));
    }
    const auto result = surge::UnifiedDecision::evaluate(converted_candidates, core_policy);
    *disposition = static_cast<int>(result.disposition);
    *selected_score = result.selected_score;
    *margin = result.margin;
    copy_text(selected_candidate_id, selected_id_capacity, result.selected_candidate_id);
    copy_text(reason, reason_capacity, result.reason);
    if (result.disposition == surge::DecisionDisposition::Select) {
      const auto selected = std::find_if(converted_candidates.begin(), converted_candidates.end(),
          [&](const surge::DecisionCandidate& item) {
            return item.candidate_id == result.selected_candidate_id;
          });
      if (selected != converted_candidates.end()) {
        for (const auto& set : selected->surface_sets) {
          const auto chosen = result.selected_surface_hypothesis_ids.find(set.source_ref);
          if (chosen == result.selected_surface_hypothesis_ids.end()) continue;
          for (size_t i = 0; i < surface_set_count; ++i) {
            if (surface_sets[i].source_ref && set.source_ref == surface_sets[i].source_ref) {
              for (size_t j = 0; j < surface_sets[i].hypothesis_count; ++j) {
                const size_t index = surface_sets[i].hypothesis_offset + j;
                if (hypotheses[index].hypothesis_id &&
                    chosen->second == hypotheses[index].hypothesis_id) {
                  selected_hypothesis_indices[i] = index;
                  break;
                }
              }
              break;
            }
          }
        }
      }
    }
    return 1;
  } catch (const std::exception& ex) {
    copy_text(reason, reason_capacity, ex.what());
    return 0;
  } catch (...) {
    copy_text(reason, reason_capacity, "UNKNOWN_CORE_EXCEPTION");
    return 0;
  }
}
}
