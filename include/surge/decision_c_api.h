#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SURGE_DECISION_C_ABI_VERSION 1u

typedef struct SurgeDecisionEvidenceInput {
  const char* evidence_id;
  const char* family;
  const char* correlation_group;
  double value;
  double weight;
  int valid;
} SurgeDecisionEvidenceInput;

typedef struct SurgeDecisionHypothesisInput {
  const char* hypothesis_id;
  size_t stable_order;
  const char* surface;
  size_t evidence_offset;
  size_t evidence_count;
} SurgeDecisionHypothesisInput;

typedef struct SurgeDecisionSurfaceSetInput {
  const char* source_ref;
  size_t hypothesis_offset;
  size_t hypothesis_count;
} SurgeDecisionSurfaceSetInput;

typedef struct SurgeDecisionCandidateInput {
  const char* candidate_id;
  size_t stable_order;
  size_t evidence_offset;
  size_t evidence_count;
  size_t surface_set_offset;
  size_t surface_set_count;
} SurgeDecisionCandidateInput;

typedef struct SurgeDecisionFamilyWeightInput {
  const char* family;
  double weight;
} SurgeDecisionFamilyWeightInput;

typedef struct SurgeDecisionPolicyInput {
  size_t max_candidates;
  size_t max_evidence_per_candidate;
  size_t max_family_weights;
  size_t max_surface_sets;
  size_t max_surface_hypotheses_total;
  size_t max_surface_trace_entries;
  size_t max_source_ref_bytes;
  size_t max_surface_bytes;
  double minimum_score;
  double minimum_margin;
  int require_valid_evidence;
} SurgeDecisionPolicyInput;

unsigned int surge_decision_c_abi_version(void);

// All arrays are flat and caller-owned. Offsets/counts must stay within the
// corresponding array. Output surface indices are SIZE_MAX when not selected.
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
    size_t* selected_hypothesis_indices, size_t selected_indices_count);

#ifdef __cplusplus
}
#endif
