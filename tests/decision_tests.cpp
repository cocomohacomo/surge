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
  auto selected = RecognitionDecision::evaluate({decoder_top, contextual});
  assert(selected.disposition == DecisionDisposition::Select);
  assert(selected.selected_candidate_id == "top2");
  assert(selected.trace.size() == 2);
  assert(selected.trace[1].score < selected.trace[0].score);

  DecisionPolicy tie_policy;
  tie_policy.minimum_margin = 0.0;
  auto tie = RecognitionDecision::evaluate({
    {"first", 0, {{"x","lexical","",1.0,1.0,true}}},
    {"second", 1, {{"y","lexical","",1.0,1.0,true}}}}, tie_policy);
  assert(tie.selected_candidate_id == "first");

  auto expired = RecognitionDecision::evaluate({
    {"expired", 0, {{"x","lexical","",100.0,1.0,false}}}});
  assert(expired.disposition == DecisionDisposition::Defer);

  DecisionPolicy strict;
  strict.max_candidates = 1;
  auto overflow = RecognitionDecision::evaluate({decoder_top, contextual}, strict);
  assert(overflow.disposition == DecisionDisposition::Defer);

  auto normalized = NormalizationDecision::evaluate({
    {"kanji", 0, {{"k","orthography","surface",0.5,1.0,true}}},
    {"kana", 1, {{"h","orthography","surface",1.0,1.0,true}}}});
  assert(normalized.selected_candidate_id == "kana");

  DecisionPolicy calibrated;
  calibrated.family_weights["semantic"] = 0.0;
  calibrated.family_weights["morphology"] = 0.0;
  auto calibration_changes_choice = RecognitionDecision::evaluate(
      {decoder_top, contextual}, calibrated);
  assert(calibration_changes_choice.disposition == DecisionDisposition::Select);
  assert(calibration_changes_choice.selected_candidate_id == "top1");

  calibrated.family_weights["lexical"] = -1.0;
  auto invalid_calibration = RecognitionDecision::evaluate({decoder_top}, calibrated);
  assert(invalid_calibration.disposition == DecisionDisposition::Reject);
  assert(invalid_calibration.reason == "RECOGNITION_INVALID_FAMILY_WEIGHT");

  DecisionPolicy overflow_calibration;
  overflow_calibration.family_weights["semantic"] = std::numeric_limits<double>::max();
  auto overflow_score = RecognitionDecision::evaluate({contextual}, overflow_calibration);
  assert(overflow_score.disposition == DecisionDisposition::Reject);
  assert(overflow_score.reason == "RECOGNITION_NONFINITE_EVIDENCE_SCORE");
  return 0;
}
