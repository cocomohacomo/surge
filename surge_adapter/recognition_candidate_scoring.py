"""Bridge compact recognition candidates to the replaceable SURGE scorer."""
from __future__ import annotations
import hashlib, json, re
from dataclasses import dataclass
from typing import Any, Mapping, Protocol
from surge_adapter.compact_recognition import RecognitionCandidate
from surge_adapter.recognition_scoring import RecognitionScorer, ScoringResult

RELATION_SCORE_FIELDS = ("relevance", "relation_strength", "novelty")

class CandidateRelationScorer(Protocol):
    def score_relations(self, candidate: RecognitionCandidate, context: Mapping[str, Any]) -> Mapping[str, float]: ...

@dataclass(frozen=True)
class CandidateAssessment:
    candidate_id: str
    packet_id: str
    source_digest: str
    candidate_index: int
    candidate: RecognitionCandidate
    scoring: ScoringResult
    @property
    def decision(self) -> str:
        return self.scoring.decision
    @property
    def commit_ready(self) -> bool:
        return False
    def as_dict(self) -> dict[str, Any]:
        c=self.candidate
        return {"candidate_id":self.candidate_id,"packet_id":self.packet_id,
                "source_digest":self.source_digest,"candidate_index":self.candidate_index,
                "candidate":{"kind":c.kind,"topic":c.topic,"proposition":c.proposition,
                "polarity":c.polarity,"conditional":c.conditional,"confidence":c.confidence,
                "ambiguity":c.ambiguity,"importance":c.importance,
                "start_offset":c.start_offset,"end_offset":c.end_offset},
                "scoring":self.scoring.as_dict(),"decision":self.decision,"commit_ready":False}

class RecognitionCandidateScoringBridge:
    def __init__(self, relation_scorer: CandidateRelationScorer, scorer: RecognitionScorer | None=None):
        self.relation_scorer=relation_scorer
        self.scorer=scorer or RecognitionScorer()
    @staticmethod
    def _candidate(raw: Mapping[str, Any]) -> RecognitionCandidate:
        fields={"kind","topic","proposition","polarity","conditional","confidence",
                "ambiguity","importance","start_offset","end_offset"}
        if set(raw)!=fields: raise ValueError("CANDIDATE_FIELDS_INVALID")
        return RecognitionCandidate(**dict(raw))
    def assess_batch(self, proposal_batch: Mapping[str, Any], *, expected_packet_id: str,
                     expected_source_digest: str, context: Mapping[str, Any] | None=None) -> tuple[CandidateAssessment,...]:
        if proposal_batch.get("schema_version")!="0.1.0": raise ValueError("PROPOSAL_BATCH_VERSION_UNSUPPORTED")
        packet_id=proposal_batch.get("packet_id")
        digest=proposal_batch.get("source_digest")
        if packet_id!=expected_packet_id: raise ValueError("PROPOSAL_PACKET_ID_MISMATCH")
        if not isinstance(digest,str) or not re.fullmatch(r"[a-fA-F0-9]{64}",digest) or digest.lower()!=expected_source_digest.lower():
            raise ValueError("PROPOSAL_SOURCE_DIGEST_MISMATCH")
        records=proposal_batch.get("candidates")
        if not isinstance(records,list): raise ValueError("PROPOSAL_CANDIDATES_INVALID")
        ctx=dict(context or {})
        result=[]
        for i,raw in enumerate(records):
            if not isinstance(raw,Mapping): raise ValueError("CANDIDATE_RECORD_INVALID")
            candidate=self._candidate(raw)
            relation=self.relation_scorer.score_relations(candidate,ctx)
            if not isinstance(relation,Mapping) or set(relation)!={"relevance","relation_strength","novelty"}:
                raise ValueError("RELATION_SCORE_FIELDS_INVALID")
            scores={"relevance":relation["relevance"],"importance":candidate.importance,
                    "relation_strength":relation["relation_strength"],"confidence":candidate.confidence,
                    "ambiguity":candidate.ambiguity,"novelty":relation["novelty"]}
            scoring=self.scorer.evaluate(f"{packet_id}:{i}",scores)
            identity={"packet_id":packet_id,"source_digest":digest.lower(),"index":i,"candidate":dict(raw)}
            cid=hashlib.sha256(json.dumps(identity,ensure_ascii=False,sort_keys=True,
                separators=(",",":"),allow_nan=False).encode()).hexdigest()
            result.append(CandidateAssessment(cid,packet_id,digest.lower(),i,candidate,scoring))
        return tuple(result)

def partition_assessments(assessments: tuple[CandidateAssessment,...]) -> dict[str,Any]:
    groups={"accepted":[],"review":[],"abstained":[]}
    for item in assessments:
        key={"ACCEPT":"accepted","REVIEW":"review","ABSTAIN":"abstained"}.get(item.decision)
        if key is None: raise ValueError("SCORING_DECISION_INVALID")
        groups[key].append(item.as_dict())
    return {**groups,"commit_ready":False}
