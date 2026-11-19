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

struct DecisionCandidate {
  std::string candidate_id;
  std::size_t stable_order{0};
  std::vector<DecisionEvidence> evidence;
};

struct DecisionPolicy {
  std::size_t max_candidates{64};
  std::size_t max_evidence_per_candidate{64};
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
};

class RecognitionDecision {
 public:
  static DecisionResult evaluate(const std::vector<DecisionCandidate>& candidates,
                                 const DecisionPolicy& policy = {});
};

class NormalizationDecision {
 public:
  static DecisionResult evaluate(const std::vector<DecisionCandidate>& hypotheses,
                                 const DecisionPolicy& policy = {});
};

}  // namespace surge
