#include "surge/cognitive_bias.hpp"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
  using namespace surge;
  CognitiveBiasInput input;
  input.has_target_cluster = true;
  input.distance = 100.0;
  input.metric = {CognitiveBiasMetricDirection::SimilarityHigherIsCloser, 0.0, 100.0};
  input.signals = {0.2, 0.1, 0.0, 0.1, 0.0};
  const auto association = CognitiveBiasEngine::evaluate(input);
  assert(association.accepted);
  assert(association.classification == CognitiveBiasClass::Association);
  assert(std::abs(association.association_affinity - 1.0) < 1e-12);
  assert(association.membership_weight > 0.0 && association.membership_weight <= 1.0);

  input.metric.direction = CognitiveBiasMetricDirection::DistanceLowerIsCloser;
  input.distance = 0.0;
  const auto lower_distance = CognitiveBiasEngine::evaluate(input);
  assert(lower_distance.accepted);
  assert(std::abs(lower_distance.association_affinity - 1.0) < 1e-12);

  input.signals = {1.0, 0.0, 0.0, 0.0, 0.0};
  const auto focused = CognitiveBiasEngine::evaluate(input);
  assert(focused.accepted);
  assert(focused.classification == CognitiveBiasClass::Focus);

  input.has_target_cluster = false;
  input.distance.reset();
  input.signals = {0.0, 0.0, 0.0, 1.0, 0.0};
  const auto novel = CognitiveBiasEngine::evaluate(input);
  assert(novel.accepted);
  assert(novel.classification == CognitiveBiasClass::Novelty);
  assert(novel.association_affinity == 0.0);

  input.distance = 0.0;
  assert(CognitiveBiasEngine::evaluate(input).error ==
      "COGNITIVE_BIAS_DISTANCE_WITHOUT_TARGET");
  input.distance.reset();
  input.signals.focus_alignment = std::numeric_limits<double>::quiet_NaN();
  assert(CognitiveBiasEngine::evaluate(input).error ==
      "COGNITIVE_BIAS_SIGNAL_OUT_OF_RANGE");

  input.signals = {};
  input.has_target_cluster = true;
  input.distance = std::numeric_limits<double>::infinity();
  assert(CognitiveBiasEngine::evaluate(input).error ==
      "COGNITIVE_BIAS_DISTANCE_PROFILE_INVALID");

  CognitiveBiasPolicy invalid_policy;
  invalid_policy.focus_weight = -1.0;
  input.distance = 1.0;
  assert(CognitiveBiasEngine::evaluate(input, invalid_policy).error ==
      "COGNITIVE_BIAS_POLICY_WEIGHT_INVALID");
  return 0;
}
