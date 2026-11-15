"""SURGE Recognition Scoring boundary v0.1."""
from __future__ import annotations
from dataclasses import dataclass
from typing import Any

SCORE_FIELDS = ("relevance", "importance", "relation_strength", "confidence", "ambiguity", "novelty")
POLICY_ID = "SURGE-SCORE-0.1-REVIEW-ONLY"

@dataclass(frozen=True)
class ScoringThresholds:
    min_confidence: float = 0.70
    max_ambiguity_for_accept: float = 0.60
    min_confidence_for_accept: float = 0.85

@dataclass(frozen=True)
class ScoringResult:
    case_id: str
    scores: dict[str, float]
    priority_score: float
    decision: str
    abstention_reasons: tuple[str, ...]
    score_policy: str
    def as_dict(self) -> dict[str, Any]:
        return {"score_version":"0.1.0","case_id":self.case_id,"scores":dict(self.scores),
                "priority_score":self.priority_score,"decision":self.decision,
                "abstention_reasons":list(self.abstention_reasons),"score_policy":self.score_policy}

class RecognitionScorer:
    """Validates replaceable recognizer outputs; has no Core commit capability."""
    def __init__(self, thresholds: ScoringThresholds | None = None):
        self.thresholds = thresholds or ScoringThresholds()

    def evaluate(self, case_id: str, scores: dict[str, float]) -> ScoringResult:
        if not case_id or set(scores) != set(SCORE_FIELDS):
            raise ValueError("SCORE_FIELDS_INVALID")
        values = {}
        for field in SCORE_FIELDS:
            value = scores[field]
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError(f"SCORE_NOT_NUMERIC:{field}")
            value = float(value)
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"SCORE_OUT_OF_RANGE:{field}")
            values[field] = value
        reasons = []
        if values["confidence"] < self.thresholds.min_confidence:
            reasons.append("LOW_CONFIDENCE")
        if values["ambiguity"] > self.thresholds.max_ambiguity_for_accept:
            reasons.append("AMBIGUOUS_REFERENCE")
        if reasons:
            decision = "ABSTAIN"
        elif values["confidence"] < self.thresholds.min_confidence_for_accept:
            decision = "REVIEW"
        else:
            decision = "ACCEPT"
        return ScoringResult(case_id, values, values["importance"], decision,
                             tuple(reasons), POLICY_ID)
