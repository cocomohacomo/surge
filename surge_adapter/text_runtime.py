"""Deterministic Japanese-first streaming text runtime (prototype).

The runtime is a replaceable-recognizer orchestration boundary. It builds an
in-memory context projection for executable semantic tests; it does not commit
C++ Core SemanticDelta records or claim production-grade NLP.
"""
from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass, field
from typing import Protocol, Any

from surge_adapter.recognition_scoring import RecognitionScorer, SCORE_FIELDS


@dataclass(frozen=True)
class TextCandidate:
    kind: str
    topic: str | None
    proposition: str | None
    polarity: str | None
    conditional: bool
    confidence: float
    ambiguity: float
    importance: float
    start: int
    end: int
    raw_text: str


class Recognizer(Protocol):
    def recognize(self, text: str) -> list[TextCandidate]: ...


class JapaneseRuleRecognizer:
    """Small deterministic recognizer used to exercise the runtime contract."""

    TOPICS = ("予算", "財源", "事業", "申請", "計画", "会議")
    FILLERS = ("えー", "あの", "その", "えっと")
    NEGATIVE = ("できません", "しません", "しない", "ではない", "ません")
    POSITIVE = ("します", "します。", "する", "確保済み", "承認します")

    def recognize(self, text: str) -> list[TextCandidate]:
        normalized = unicodedata.normalize("NFC", text)
        topic = next((word for word in self.TOPICS if word in normalized), None)
        stripped = normalized.strip()
        if not stripped:
            return []
        if all(token in stripped for token in self.FILLERS) or stripped in self.FILLERS:
            return [self._candidate("FILLER", topic, None, None, False, .99, .02, .02, normalized)]
        if any(x in stripped for x in ("ですか", "でしょうか", "ませんか", "本当ですか", "なぜ")):
            return [self._candidate("QUESTION", topic, self._proposition(stripped), None, False, .96, .08, .82, normalized)]
        if any(x in stripped for x in ("訂正します", "ではなく", "ではありません")):
            return [self._candidate("CORRECTION", topic, self._proposition(stripped), None, False, .90, .18, .98, normalized)]
        if stripped in ("うん", "はい", "そうですね", "それでお願いします"):
            kind = "DECISION_ACCEPTANCE" if stripped == "それでお願いします" else "ACKNOWLEDGEMENT"
            importance = .96 if kind == "DECISION_ACCEPTANCE" else .62
            return [self._candidate(kind, topic, None, None, False, .94, .12, importance, normalized)]
        if any(x in stripped for x in self.NEGATIVE):
            return [self._candidate("CLAIM", topic, self._proposition(stripped), "NEGATIVE",
                                    any(x in stripped for x in ("場合", "なら", "れば")), .94, .12, .88, normalized)]
        if any(x in stripped for x in self.POSITIVE):
            return [self._candidate("CLAIM", topic, self._proposition(stripped), "POSITIVE",
                                    any(x in stripped for x in ("場合", "なら", "れば")), .92, .14, .84, normalized)]
        if any(x in stripped for x in ("場合", "なら", "れば")):
            return [self._candidate("CONDITIONAL", topic, self._proposition(stripped), None, True, .82, .25, .76, normalized)]
        if any(x in stripped for x in ("それ", "これ", "あれ")) and topic is None:
            return [self._candidate("UNRESOLVED_REFERENCE", None, None, None, False, .58, .78, .52, normalized)]
        return [self._candidate("UNCLASSIFIED", topic, None, None, False, .55, .55, .30, normalized)]

    @staticmethod
    def _proposition(text: str) -> str:
        value = text
        for token in ("訂正します", "ではなく", "ではありません", "できません", "しません", "しない", "ません", "します", "する", "です", "。", "、"):
            value = value.replace(token, "")
        value = re.sub(r"\s+", "", value)
        return value or text

    @staticmethod
    def _candidate(kind: str, topic: str | None, proposition: str | None,
                   polarity: str | None, conditional: bool, confidence: float,
                   ambiguity: float, importance: float, text: str) -> TextCandidate:
        raw = text
        return TextCandidate(kind, topic, proposition, polarity, conditional,
                             confidence, ambiguity, importance, 0,
                             len(raw.encode("utf-8")), raw)


@dataclass
class StreamingContextRuntime:
    recognizer: Recognizer = field(default_factory=JapaneseRuleRecognizer)
    scorer: RecognitionScorer = field(default_factory=RecognitionScorer)
    clusters: dict[str, dict[str, Any]] = field(default_factory=dict)
    claims: list[dict[str, Any]] = field(default_factory=list)
    relations: list[dict[str, Any]] = field(default_factory=list)
    questions: list[dict[str, Any]] = field(default_factory=list)
    interests: list[dict[str, Any]] = field(default_factory=list)
    events: list[dict[str, Any]] = field(default_factory=list)
    active_cluster: str | None = None
    _sequence: int = 0

    def process(self, text: str) -> dict[str, Any]:
        self._sequence += 1
        candidates = self.recognizer.recognize(text)
        emitted: list[dict[str, Any]] = []
        for index, candidate in enumerate(candidates):
            scores = {
                "relevance": .85 if candidate.topic and self.active_cluster == f"cluster:{candidate.topic}" else .55,
                "importance": candidate.importance,
                "relation_strength": .70 if self.claims else .25,
                "confidence": candidate.confidence,
                "ambiguity": candidate.ambiguity,
                "novelty": .75 if candidate.kind in ("CLAIM", "CORRECTION", "QUESTION") else .25,
            }
            score = self.scorer.evaluate(f"stream-{self._sequence}-{index}", scores)
            topic = candidate.topic
            if topic:
                cluster_id = f"cluster:{topic}"
                self.clusters.setdefault(cluster_id, {"id": cluster_id, "topic": topic, "claim_refs": [], "event_refs": []})
                self.active_cluster = cluster_id
            else:
                cluster_id = self.active_cluster

            event = {"id": f"event:{self._sequence}:{index}", "sequence": self._sequence,
                     "kind": candidate.kind, "cluster_ref": cluster_id,
                     "decision": score.decision, "scores": score.as_dict()["scores"],
                     "raw_text": text}
            self.events.append(event)
            emitted.append(event)
            if cluster_id:
                self.clusters[cluster_id]["event_refs"].append(event["id"])

            if candidate.kind == "UNRESOLVED_REFERENCE" or score.decision == "ABSTAIN":
                event["resolution"] = "UNRESOLVED"
                continue
            if candidate.kind in ("CLAIM", "CONDITIONAL") and candidate.proposition:
                claim = {"id": f"claim:{self._sequence}", "cluster_ref": cluster_id,
                         "proposition": candidate.proposition, "polarity": candidate.polarity,
                         "conditional": candidate.conditional, "status": "ACTIVE",
                         "source_event": event["id"]}
                for previous in self.claims:
                    if previous["proposition"] == claim["proposition"] and previous["polarity"] and claim["polarity"] and previous["polarity"] != claim["polarity"]:
                        self.relations.append({"type": "CONTRADICTS", "from": previous["id"], "to": claim["id"]})
                        question = {"id": f"question:{self._sequence}", "reason": "CONTRADICTION",
                                    "claim_refs": [previous["id"], claim["id"]]}
                        self.questions.append(question)
                        self.interests.append({"cluster_ref": previous["cluster_ref"], "question_ref": question["id"]})
                self.claims.append(claim)
                if cluster_id:
                    self.clusters[cluster_id]["claim_refs"].append(claim["id"])
                event["claim_ref"] = claim["id"]
            elif candidate.kind == "CORRECTION":
                prior = next((c for c in reversed(self.claims) if c["cluster_ref"] == cluster_id), None)
                if prior:
                    prior["status"] = "SUPERSEDED"
                    self.relations.append({"type": "REVISION_OF", "from": f"claim:{self._sequence}", "to": prior["id"]})
                event["revision_candidate"] = True
            elif candidate.kind == "QUESTION":
                q = {"id": f"question:{self._sequence}", "reason": "EXPLICIT_QUESTION", "cluster_ref": cluster_id}
                self.questions.append(q)
                if cluster_id:
                    self.interests.append({"cluster_ref": cluster_id, "question_ref": q["id"]})
            elif candidate.kind in ("ACKNOWLEDGEMENT", "DECISION_ACCEPTANCE"):
                event["attached_to"] = cluster_id
        return {"sequence": self._sequence, "events": emitted,
                "active_cluster": self.active_cluster,
                "cluster_count": len(self.clusters), "claim_count": len(self.claims),
                "question_count": len(self.questions), "interest_count": len(self.interests),
                "relations": list(self.relations)}

    def snapshot(self) -> dict[str, Any]:
        return {"active_cluster": self.active_cluster,
                "clusters": list(self.clusters.values()), "claims": list(self.claims),
                "relations": list(self.relations), "questions": list(self.questions),
                "interests": list(self.interests), "events": list(self.events)}
