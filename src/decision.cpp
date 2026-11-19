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
  if (!std::isfinite(policy.minimum_score) ||
      !std::isfinite(policy.minimum_margin) || policy.minimum_margin < 0.0) {
    result.disposition = DecisionDisposition::Reject;
    result.reason = std::string(stage) + "_INVALID_POLICY_THRESHOLD";
    return result;
  }
  if (policy.max_candidates == 0 || candidates.size() > policy.max_candidates) {
    result.disposition = DecisionDisposition::Defer;
    result.reason = std::string(stage) + "_CANDIDATE_BUDGET_EXCEEDED";
    return result;
  }
  if (policy.family_weights.size() > policy.max_family_weights) {
    result.disposition = DecisionDisposition::Defer;
    result.reason = std::string(stage) + "_FAMILY_WEIGHT_BUDGET_EXCEEDED";
    return result;
  }
  for (const auto& item : policy.family_weights) {
    if (item.first.empty() || !std::isfinite(item.second) || item.second < 0.0) {
      result.disposition = DecisionDisposition::Reject;
      result.reason = std::string(stage) + "_INVALID_FAMILY_WEIGHT";
      return result;
    }
  }
  if (policy.max_evidence_per_candidate == 0 || policy.max_family_weights == 0 ||
      policy.max_source_ref_bytes == 0 || policy.max_surface_bytes == 0) {
    result.disposition = DecisionDisposition::Reject;
    result.reason = std::string(stage) + "_INVALID_RESOURCE_POLICY";
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
    if (candidate.candidate_id.size() > 1024) {
      result.disposition = DecisionDisposition::Defer;
      result.reason = std::string(stage) + "_IDENTIFIER_BUDGET_EXCEEDED";
      return result;
    }
    std::map<std::pair<std::string, std::string>, double> groups;
    std::set<std::string> evidence_ids;
    bool effective_evidence = false;
    Scored item;
    item.trace.candidate_id = candidate.candidate_id;
    item.stable_order = candidate.stable_order;
    for (const auto& evidence : candidate.evidence) {
      if (evidence.evidence_id.empty() || evidence.evidence_id.size() > 1024 ||
          evidence.family.size() > 128 || evidence.correlation_group.size() > 1024 ||
          !evidence_ids.insert(evidence.evidence_id).second ||
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
      const auto family_weight = policy.family_weights.find(evidence.family);
      const double calibration = family_weight == policy.family_weights.end()
          ? 1.0 : family_weight->second;
      if (!std::isfinite(calibration) || calibration < 0.0) {
        result.disposition = DecisionDisposition::Reject;
        result.reason = std::string(stage) + "_INVALID_FAMILY_WEIGHT";
        return result;
      }
      const auto group = std::make_pair(evidence.family,
          evidence.correlation_group.empty() ? evidence.evidence_id : evidence.correlation_group);
      const double contribution = evidence.value * evidence.weight * calibration;
      if (!std::isfinite(contribution)) {
        result.disposition = DecisionDisposition::Reject;
        result.reason = std::string(stage) + "_NONFINITE_EVIDENCE_SCORE";
        return result;
      }
      const auto found = groups.find(group);
      if (found == groups.end() || std::abs(contribution) > std::abs(found->second))
        groups[group] = contribution;
      if (contribution != 0.0) effective_evidence = true;
      ++item.trace.accepted_evidence;
    }
    if ((policy.require_valid_evidence && item.trace.accepted_evidence == 0) ||
        !effective_evidence) {
      item.trace.score = -INFINITY;
    } else {
      for (const auto& group : groups) {
        item.trace.score += group.second;
        if (!std::isfinite(item.trace.score)) {
          result.disposition = DecisionDisposition::Reject;
          result.reason = std::string(stage) + "_NONFINITE_CANDIDATE_SCORE";
          return result;
        }
      }
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

DecisionResult UnifiedDecision::evaluate(
    const std::vector<DecisionCandidate>& candidates, const DecisionPolicy& policy) {
  DecisionResult result = evaluate_impl(candidates, policy, "UNIFIED");
  if (result.disposition != DecisionDisposition::Select) return result;

  const auto selected = std::find_if(candidates.begin(), candidates.end(),
      [&](const DecisionCandidate& candidate) {
        return candidate.candidate_id == result.selected_candidate_id;
      });
  if (selected == candidates.end()) {
    result.disposition = DecisionDisposition::Reject;
    result.reason = "UNIFIED_SELECTED_CANDIDATE_NOT_FOUND";
    result.selected_candidate_id.clear();
    return result;
  }

  if (selected->surface_sets.size() > policy.max_surface_sets) {
    result.disposition = DecisionDisposition::Defer;
    result.reason = "UNIFIED_SURFACE_SET_BUDGET_EXCEEDED";
    result.selected_surfaces.clear();
    return result;
  }
  std::size_t total_hypotheses = 0;
  std::size_t total_trace_entries = 0;
  std::set<std::string> source_refs;
  for (const auto& surface_set : selected->surface_sets) {
    if (surface_set.hypotheses.size() > policy.max_surface_hypotheses_total -
        std::min(total_hypotheses, policy.max_surface_hypotheses_total)) {
      result.disposition = DecisionDisposition::Defer;
      result.reason = "UNIFIED_SURFACE_HYPOTHESIS_BUDGET_EXCEEDED";
      result.selected_surfaces.clear();
      return result;
    }
    total_hypotheses += surface_set.hypotheses.size();
    if (surface_set.source_ref.size() > policy.max_source_ref_bytes) {
      result.disposition = DecisionDisposition::Defer;
      result.reason = "UNIFIED_SOURCE_REF_BUDGET_EXCEEDED";
      result.selected_surfaces.clear();
      return result;
    }
    if (surface_set.source_ref.empty() ||
        !source_refs.insert(surface_set.source_ref).second ||
        surface_set.hypotheses.empty()) {
      result.disposition = DecisionDisposition::Reject;
      result.reason = "UNIFIED_INVALID_SURFACE_SET";
      result.selected_surfaces.clear();
      return result;
    }
    std::vector<DecisionCandidate> hypotheses;
    hypotheses.reserve(surface_set.hypotheses.size());
    for (const auto& hypothesis : surface_set.hypotheses) {
      if (hypothesis.surface.size() > policy.max_surface_bytes) {
        result.disposition = DecisionDisposition::Defer;
        result.reason = "UNIFIED_SURFACE_BYTES_BUDGET_EXCEEDED";
        result.selected_surfaces.clear();
        return result;
      }
      if (hypothesis.surface.empty()) {
        result.disposition = DecisionDisposition::Reject;
        result.reason = "UNIFIED_EMPTY_SURFACE";
        result.selected_surfaces.clear();
        return result;
      }
      DecisionCandidate candidate;
      candidate.candidate_id = hypothesis.hypothesis_id;
      candidate.stable_order = hypothesis.stable_order;
      candidate.evidence = hypothesis.evidence;
      hypotheses.push_back(std::move(candidate));
    }
    const auto surface = evaluate_impl(hypotheses, policy, "UNIFIED_SURFACE");
    if (surface.trace.size() > policy.max_surface_trace_entries -
        std::min(total_trace_entries, policy.max_surface_trace_entries)) {
      result.disposition = DecisionDisposition::Defer;
      result.reason = "UNIFIED_SURFACE_TRACE_BUDGET_EXCEEDED";
      result.selected_surfaces.clear();
      result.surface_trace.clear();
      return result;
    }
    total_trace_entries += surface.trace.size();
    result.surface_trace.insert(result.surface_trace.end(),
        surface.trace.begin(), surface.trace.end());
    if (surface.disposition != DecisionDisposition::Select) {
      result.disposition = surface.disposition;
      result.reason = surface.reason;
      result.selected_surfaces.clear();
      return result;
    }
    const auto chosen = std::find_if(surface_set.hypotheses.begin(),
        surface_set.hypotheses.end(), [&](const DecisionSurfaceHypothesis& item) {
          return item.hypothesis_id == surface.selected_candidate_id;
        });
    if (chosen == surface_set.hypotheses.end()) {
      result.disposition = DecisionDisposition::Reject;
      result.reason = "UNIFIED_SELECTED_SURFACE_NOT_FOUND";
      result.selected_surfaces.clear();
      return result;
    }
    result.selected_surfaces.emplace(surface_set.source_ref, chosen->surface);
    result.selected_surface_hypothesis_ids.emplace(surface_set.source_ref, chosen->hypothesis_id);
  }
  result.reason = "UNIFIED_CORE_SELECTED";
  return result;
}
}  // namespace surge
