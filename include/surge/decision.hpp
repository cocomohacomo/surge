#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace surge {

enum class DecisionDisposition { Select, Defer, Reject };

struct DecisionEvidence {
  std::string evidence_id;
  std::string family;
  std::string correlation_group;
  double value{0.0};
  double weight{1.0};
  bool valid{true};
};

struct DecisionSurfaceHypothesis {
  std::string hypothesis_id;
  std::size_t stable_order{0};
  std::string surface;
  std::vector<DecisionEvidence> evidence;
};

struct DecisionSurfaceSet {
  std::string source_ref;
  std::vector<DecisionSurfaceHypothesis> hypotheses;
};

struct DecisionCandidate {
  std::string candidate_id;
  std::size_t stable_order{0};
  std::vector<DecisionEvidence> evidence;
  // Surface realization is evaluated in the same Core decision transaction.
  std::vector<DecisionSurfaceSet> surface_sets;
};

struct DecisionPolicy {
  std::size_t max_candidates{64};
  std::size_t max_evidence_per_candidate{64};
  std::size_t max_family_weights{64};
  // Aggregate realization limits across the selected candidate.
  std::size_t max_surface_sets{256};
  std::size_t max_surface_hypotheses_total{4096};
  std::size_t max_surface_trace_entries{4096};
  std::size_t max_source_ref_bytes{1024};
  std::size_t max_surface_bytes{4096};
  double minimum_score{-1.0e9};
  double minimum_margin{0.0};
  bool require_valid_evidence{true};
  // Optional multiplicative calibration by evidence family. Missing families use 1.0.
  std::map<std::string, double> family_weights;
};

struct CandidateDecisionTrace {
  std::string candidate_id;
  double score{0.0};
  std::size_t accepted_evidence{0};
  std::size_t ignored_evidence{0};
};

struct DecisionResult {
  DecisionDisposition disposition{DecisionDisposition::Defer};
  std::string selected_candidate_id;
  std::string reason;
  double selected_score{0.0};
  double margin{0.0};
  std::vector<CandidateDecisionTrace> trace;
  std::map<std::string, std::string> selected_surfaces;
  std::map<std::string, std::string> selected_surface_hypothesis_ids;
  std::vector<CandidateDecisionTrace> surface_trace;
};

// One atomic Core decision: interpretation ranking and surface realization are
// evaluated together under one policy, trace and disposition.
class UnifiedDecision {
 public:
  static DecisionResult evaluate(const std::vector<DecisionCandidate>& candidates,
                                 const DecisionPolicy& policy = {});
};

}  // namespace surge
