#include "surge/decision.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace surge {
namespace {
struct Scored {
  CandidateDecisionTrace trace;
  std::size_t stable_order{0};
};

DecisionResult evaluate_impl(const std::vector<DecisionCandidate>& candidates,
                             const DecisionPolicy& policy,
                             const char* stage) {
  DecisionResult result;
  if (candidates.empty()) {
    result.disposition = DecisionDisposition::Reject;
    result.reason = std::string(stage) + "_NO_CANDIDATES";
    return result;
  }
  if (policy.max_candidates == 0 || candidates.size() > policy.max_candidates) {
    result.disposition = DecisionDisposition::Defer;
    result.reason = std::string(stage) + "_CANDIDATE_BUDGET_EXCEEDED";
    return result;
  }
  std::set<std::string> ids;
  std::vector<Scored> scored;
  scored.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    if (candidate.candidate_id.empty() || !ids.insert(candidate.candidate_id).second) {
      result.disposition = DecisionDisposition::Reject;
      result.reason = std::string(stage) + "_INVALID_OR_DUPLICATE_CANDIDATE_ID";
      return result;
    }
    if (candidate.evidence.size() > policy.max_evidence_per_candidate) {
      result.disposition = DecisionDisposition::Defer;
      result.reason = std::string(stage) + "_EVIDENCE_BUDGET_EXCEEDED";
      return result;
    }
    // Correlated evidence is grouped by (family, correlation_group). Only the
    // strongest positive/negative observation in each group contributes.
    std::map<std::pair<std::string, std::string>, double> groups;
    std::set<std::string> evidence_ids;
    Scored item;
    item.trace.candidate_id = candidate.candidate_id;
    item.stable_order = candidate.stable_order;
    for (const auto& evidence : candidate.evidence) {
      if (evidence.evidence_id.empty() || !evidence_ids.insert(evidence.evidence_id).second ||
          evidence.family.empty() || !std::isfinite(evidence.value) ||
          !std::isfinite(evidence.weight) || evidence.weight < 0.0) {
        result.disposition = DecisionDisposition::Reject;
        result.reason = std::string(stage) + "_INVALID_EVIDENCE";
        return result;
      }
      if (!evidence.valid) {
        ++item.trace.ignored_evidence;
        continue;
      }
      const auto group = std::make_pair(evidence.family,
          evidence.correlation_group.empty() ? evidence.evidence_id : evidence.correlation_group);
      const double contribution = evidence.value * evidence.weight;
      const auto found = groups.find(group);
      if (found == groups.end() || std::abs(contribution) > std::abs(found->second))
        groups[group] = contribution;
      ++item.trace.accepted_evidence;
    }
    if (policy.require_valid_evidence && item.trace.accepted_evidence == 0) {
      item.trace.score = -INFINITY;
    } else {
      for (const auto& group : groups) item.trace.score += group.second;
    }
    scored.push_back(std::move(item));
  }
  std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    if (a.trace.score != b.trace.score) return a.trace.score > b.trace.score;
    if (a.stable_order != b.stable_order) return a.stable_order < b.stable_order;
    return a.trace.candidate_id < b.trace.candidate_id;
  });
  for (const auto& item : scored) result.trace.push_back(item.trace);
  const auto& best = scored.front().trace;
  const double second = scored.size() > 1 ? scored[1].trace.score : -INFINITY;
  result.selected_score = best.score;
  result.margin = std::isfinite(second) ? best.score - second : INFINITY;
  if (!std::isfinite(best.score) || best.score < policy.minimum_score) {
    result.disposition = DecisionDisposition::Defer;
    result.reason = std::string(stage) + "_INSUFFICIENT_VALID_EVIDENCE";
    return result;
  }
  if (scored.size() > 1 && result.margin < policy.minimum_margin) {
    result.disposition = DecisionDisposition::Defer;
    result.reason = std::string(stage) + "_AMBIGUOUS_MARGIN";
    return result;
  }
  result.disposition = DecisionDisposition::Select;
  result.selected_candidate_id = best.candidate_id;
  result.reason = std::string(stage) + "_CORE_SELECTED";
  return result;
}
}  // namespace

DecisionResult RecognitionDecision::evaluate(const std::vector<DecisionCandidate>& candidates,
                                               const DecisionPolicy& policy) {
  return evaluate_impl(candidates, policy, "RECOGNITION");
}
DecisionResult NormalizationDecision::evaluate(const std::vector<DecisionCandidate>& hypotheses,
                                                 const DecisionPolicy& policy) {
  return evaluate_impl(hypotheses, policy, "NORMALIZATION");
}
}  // namespace surge
