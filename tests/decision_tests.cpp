#include "surge/decision.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

int main() {
  using namespace surge;
  DecisionCandidate decoder_top{"top1", 0, {{"a","lexical","dict",0.2,1.0,true}}};
  DecisionCandidate contextual{"top2", 1, {
    {"b","semantic","context",1.2,1.0,true},
    {"c","semantic","context",1.0,1.0,true},
    {"d","morphology","morph",0.3,1.0,true}}};
  auto selected = UnifiedDecision::evaluate({decoder_top, contextual});
  assert(selected.disposition == DecisionDisposition::Select);
  assert(selected.selected_candidate_id == "top2");
  assert(selected.trace.size() == 2);
  assert(selected.trace[1].score < selected.trace[0].score);

  DecisionPolicy tie_policy;
  tie_policy.minimum_margin = 0.0;
  auto tie = UnifiedDecision::evaluate({
    {"first", 0, {{"x","lexical","",1.0,1.0,true}}},
    {"second", 1, {{"y","lexical","",1.0,1.0,true}}}}, tie_policy);
  assert(tie.selected_candidate_id == "first");

  auto expired = UnifiedDecision::evaluate({
    {"expired", 0, {{"x","lexical","",100.0,1.0,false}}}});
  assert(expired.disposition == DecisionDisposition::Defer);

  DecisionPolicy strict;
  strict.max_candidates = 1;
  auto overflow = UnifiedDecision::evaluate({decoder_top, contextual}, strict);
  assert(overflow.disposition == DecisionDisposition::Defer);

  DecisionCandidate joint;
  joint.candidate_id = "recognized-path";
  joint.stable_order = 0;
  joint.evidence.push_back({"recognition", "lexical", "token", 1.0, 1.0, true});
  joint.surface_sets.push_back({"span:0:2", {
    {"kanji", 0, "漢字", {{"k","orthography","kanji",0.5,1.0,true}}},
    {"kana", 1, "かな", {{"h","orthography","kana",1.0,1.0,true}}}}});
  auto normalized = UnifiedDecision::evaluate({joint});
  assert(normalized.disposition == DecisionDisposition::Select);
  assert(normalized.selected_candidate_id == "recognized-path");
  assert(normalized.selected_surfaces.at("span:0:2") == "かな");
  assert(normalized.surface_trace.size() == 2);

  auto unresolved_surface = joint;
  unresolved_surface.surface_sets.front().hypotheses[1].evidence.front().valid = false;
  unresolved_surface.surface_sets.front().hypotheses[0].evidence.front().valid = false;
  auto deferred_joint = UnifiedDecision::evaluate({unresolved_surface});
  assert(deferred_joint.disposition == DecisionDisposition::Defer);
  assert(deferred_joint.selected_surfaces.empty());
  assert(deferred_joint.selected_surface_hypothesis_ids.empty());

  auto partial_surface = joint;
  partial_surface.surface_sets.push_back({"span:2:4", {
    {"left", 0, "左", {{"l","orthography","left",1.0,1.0,true}}},
    {"right", 1, "右", {{"r","orthography","right",1.0,1.0,true}}}}});
  DecisionPolicy require_surface_margin;
  require_surface_margin.minimum_margin = 0.1;
  auto no_partial_surface = UnifiedDecision::evaluate({partial_surface}, require_surface_margin);
  assert(no_partial_surface.disposition == DecisionDisposition::Defer);
  assert(no_partial_surface.selected_surfaces.empty());
  assert(no_partial_surface.selected_surface_hypothesis_ids.empty());

  DecisionPolicy calibrated;
  calibrated.family_weights["semantic"] = 0.0;
  calibrated.family_weights["morphology"] = 0.0;
  auto calibration_changes_choice = UnifiedDecision::evaluate(
      {decoder_top, contextual}, calibrated);
  assert(calibration_changes_choice.disposition == DecisionDisposition::Select);
  assert(calibration_changes_choice.selected_candidate_id == "top1");

  calibrated.family_weights["lexical"] = -1.0;
  auto invalid_calibration = UnifiedDecision::evaluate({decoder_top}, calibrated);
  assert(invalid_calibration.disposition == DecisionDisposition::Reject);
  assert(invalid_calibration.reason == "UNIFIED_INVALID_FAMILY_WEIGHT");

  DecisionPolicy overflow_calibration;
  overflow_calibration.family_weights["semantic"] = std::numeric_limits<double>::max();
  auto overflow_score = UnifiedDecision::evaluate({contextual}, overflow_calibration);
  assert(overflow_score.disposition == DecisionDisposition::Reject);
  assert(overflow_score.reason == "UNIFIED_NONFINITE_EVIDENCE_SCORE");

  DecisionPolicy sum_overflow_policy;
  sum_overflow_policy.family_weights["family_a"] = std::numeric_limits<double>::max();
  sum_overflow_policy.family_weights["family_b"] = std::numeric_limits<double>::max();
  auto sum_overflow = UnifiedDecision::evaluate({
    {"sum-overflow", 0, {
      {"e1", "family_a", "a", 1.0, 1.0, true},
      {"e2", "family_b", "b", 1.0, 1.0, true}}}}, sum_overflow_policy);
  assert(sum_overflow.disposition == DecisionDisposition::Reject);
  assert(sum_overflow.reason == "UNIFIED_NONFINITE_CANDIDATE_SCORE");

  DecisionPolicy family_budget;
  family_budget.max_family_weights = 1;
  family_budget.family_weights["one"] = 1.0;
  family_budget.family_weights["two"] = 1.0;
  auto family_overflow = UnifiedDecision::evaluate({decoder_top}, family_budget);
  assert(family_overflow.disposition == DecisionDisposition::Defer);
  assert(family_overflow.reason == "RECOGNITION_FAMILY_WEIGHT_BUDGET_EXCEEDED");
  // Valid but non-discriminating evidence must never authorize a selection.
  DecisionPolicy zero_weight;
  zero_weight.family_weights["lexical"] = 0.0;
  auto zero_contribution = UnifiedDecision::evaluate({decoder_top}, zero_weight);
  assert(zero_contribution.disposition == DecisionDisposition::Defer);
  assert(zero_contribution.reason == "UNIFIED_INSUFFICIENT_VALID_EVIDENCE");

  DecisionPolicy invalid_threshold;
  invalid_threshold.minimum_margin = std::numeric_limits<double>::quiet_NaN();
  auto nan_threshold = UnifiedDecision::evaluate({decoder_top}, invalid_threshold);
  assert(nan_threshold.disposition == DecisionDisposition::Reject);
  assert(nan_threshold.reason == "UNIFIED_INVALID_POLICY_THRESHOLD");
  invalid_threshold.minimum_margin = -0.1;
  auto negative_margin = UnifiedDecision::evaluate({decoder_top}, invalid_threshold);
  assert(negative_margin.disposition == DecisionDisposition::Reject);

  DecisionPolicy surface_budget;
  surface_budget.max_surface_hypotheses_total = 1;
  auto surface_overflow = UnifiedDecision::evaluate({joint}, surface_budget);
  assert(surface_overflow.disposition == DecisionDisposition::Defer);
  assert(surface_overflow.reason == "UNIFIED_SURFACE_HYPOTHESIS_BUDGET_EXCEEDED");
  assert(surface_overflow.selected_surfaces.empty());

  DecisionPolicy source_budget;
  source_budget.max_source_ref_bytes = 4;
  auto source_overflow = UnifiedDecision::evaluate({joint}, source_budget);
  assert(source_overflow.disposition == DecisionDisposition::Defer);
  assert(source_overflow.reason == "UNIFIED_SOURCE_REF_BUDGET_EXCEEDED");

  DecisionPolicy surface_evidence_budget;
  surface_evidence_budget.max_surface_evidence_total = 1;
  auto surface_evidence_overflow = UnifiedDecision::evaluate({joint}, surface_evidence_budget);
  assert(surface_evidence_overflow.disposition == DecisionDisposition::Defer);
  assert(surface_evidence_overflow.reason == "UNIFIED_SURFACE_EVIDENCE_BUDGET_EXCEEDED");
  assert(surface_evidence_overflow.selected_surfaces.empty());
  assert(surface_evidence_overflow.surface_trace.empty());

  return 0;
}
