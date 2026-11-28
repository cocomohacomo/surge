#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace surge {

enum class CognitiveBiasMetricDirection {
  DistanceLowerIsCloser,
  SimilarityHigherIsCloser
};

enum class CognitiveBiasClass {
  Association,
  Focus,
  Interest,
  QuestionPressure,
  Novelty,
  Recurrence,
  Unresolved
};

struct CognitiveBiasMetric {
  CognitiveBiasMetricDirection direction{CognitiveBiasMetricDirection::SimilarityHigherIsCloser};
  double minimum{0.0};
  double maximum{1.0};
};

struct CognitiveBiasSignals {
  // These signals are read from SURGE-owned state, never from the Projector.
  double focus_alignment{0.0};
  double interest_activation{0.0};
  double unresolved_question_pressure{0.0};
  double novelty{0.0};
  double recurrence{0.0};
};

struct CognitiveBiasPolicy {
  std::string policy_id{"surge-cognitive-bias"};
  std::string policy_version{"0.1.0"};
  double association_weight{0.35};
  double focus_weight{0.20};
  double interest_weight{0.15};
  double question_weight{0.15};
  double novelty_weight{0.10};
  double recurrence_weight{0.05};
};

struct CognitiveBiasInput {
  bool has_target_cluster{false};
  std::optional<double> distance;
  CognitiveBiasMetric metric;
  CognitiveBiasSignals signals;
};

struct CognitiveBiasResult {
  bool accepted{false};
  std::string error;
  CognitiveBiasClass classification{CognitiveBiasClass::Unresolved};
  double membership_weight{0.0};
  double association_affinity{0.0};
  std::string policy_id;
  std::string policy_version;
};

class CognitiveBiasEngine {
 public:
  static CognitiveBiasResult evaluate(const CognitiveBiasInput& input,
                                      const CognitiveBiasPolicy& policy = {}) {
    CognitiveBiasResult result;
    result.policy_id = policy.policy_id;
    result.policy_version = policy.policy_version;
    if (policy.policy_id.empty() || policy.policy_version.empty()) {
      result.error = "COGNITIVE_BIAS_POLICY_IDENTITY_INVALID";
      return result;
    }
    const double weights[] = {policy.association_weight, policy.focus_weight,
      policy.interest_weight, policy.question_weight, policy.novelty_weight,
      policy.recurrence_weight};
    double weight_sum = 0.0;
    for (double weight : weights) {
      if (!std::isfinite(weight) || weight < 0.0) {
        result.error = "COGNITIVE_BIAS_POLICY_WEIGHT_INVALID";
        return result;
      }
      weight_sum += weight;
    }
    if (!std::isfinite(weight_sum) || weight_sum <= 0.0) {
      result.error = "COGNITIVE_BIAS_POLICY_WEIGHT_SUM_INVALID";
      return result;
    }
    const double signals[] = {input.signals.focus_alignment,
      input.signals.interest_activation, input.signals.unresolved_question_pressure,
      input.signals.novelty, input.signals.recurrence};
    for (double signal : signals) {
      if (!std::isfinite(signal) || signal < 0.0 || signal > 1.0) {
        result.error = "COGNITIVE_BIAS_SIGNAL_OUT_OF_RANGE";
        return result;
      }
    }

    if (input.has_target_cluster) {
      if (!input.distance || !std::isfinite(*input.distance) ||
          !std::isfinite(input.metric.minimum) || !std::isfinite(input.metric.maximum) ||
          input.metric.maximum <= input.metric.minimum ||
          *input.distance < input.metric.minimum || *input.distance > input.metric.maximum) {
        result.error = "COGNITIVE_BIAS_DISTANCE_PROFILE_INVALID";
        return result;
      }
      const double normalized = (*input.distance - input.metric.minimum) /
          (input.metric.maximum - input.metric.minimum);
      result.association_affinity =
          input.metric.direction == CognitiveBiasMetricDirection::DistanceLowerIsCloser
              ? 1.0 - normalized : normalized;
    } else if (input.distance) {
      result.error = "COGNITIVE_BIAS_DISTANCE_WITHOUT_TARGET";
      return result;
    }

    const double contributions[] = {
      input.has_target_cluster ? policy.association_weight * result.association_affinity : 0.0,
      policy.focus_weight * input.signals.focus_alignment,
      policy.interest_weight * input.signals.interest_activation,
      policy.question_weight * input.signals.unresolved_question_pressure,
      policy.novelty_weight * input.signals.novelty,
      policy.recurrence_weight * input.signals.recurrence
    };
    const CognitiveBiasClass classes[] = {
      CognitiveBiasClass::Association, CognitiveBiasClass::Focus,
      CognitiveBiasClass::Interest, CognitiveBiasClass::QuestionPressure,
      CognitiveBiasClass::Novelty, CognitiveBiasClass::Recurrence
    };
    double total = 0.0;
    std::size_t selected = 0;
    double strongest = -1.0;
    for (std::size_t i = 0; i < 6; ++i) {
      total += contributions[i];
      if (contributions[i] > strongest) {
        strongest = contributions[i];
        selected = i;
      }
    }
    result.membership_weight = std::clamp(total / weight_sum, 0.0, 1.0);
    result.classification = strongest <= 0.0
        ? CognitiveBiasClass::Unresolved : classes[selected];
    result.accepted = true;
    result.error.clear();
    return result;
  }
};

inline const char* cognitive_bias_class_name(CognitiveBiasClass value) {
  switch (value) {
    case CognitiveBiasClass::Association: return "ASSOCIATION";
    case CognitiveBiasClass::Focus: return "FOCUS";
    case CognitiveBiasClass::Interest: return "INTEREST";
    case CognitiveBiasClass::QuestionPressure: return "QUESTION_PRESSURE";
    case CognitiveBiasClass::Novelty: return "NOVELTY";
    case CognitiveBiasClass::Recurrence: return "RECURRENCE";
    case CognitiveBiasClass::Unresolved: return "UNRESOLVED";
  }
  return "UNRESOLVED";
}

}  // namespace surge
