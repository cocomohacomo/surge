"""Web document recognition and lifecycle layer for SURGE v0.1.

The provider proposes claims; this module owns chunk/source coordinates, scope
anchors, applicability comparison, temporal state, provenance, and dedup policy.
The C++ Core remains the only semantic commit authority.
"""
from __future__ import annotations

import hashlib
import json
import re
import sqlite3
import time
import unicodedata
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import urlsplit, urlunsplit

from surge_adapter.llm_interface import LLMRecognizer
from surge_adapter.packet_ingest import RecognitionPacketAdapter, normalize_source
from surge_adapter.llm_interface import LLMSemanticRuntime

TEMPORAL_PATTERNS = (
    ("CANCELLED", r"中止|取消|撤回|取りやめ|廃止"),
    ("COMPLETED", r"実施した|開催した|完了した|公表した|提出した|決定した"),
    ("UNDER_REVIEW", r"検討中|審議中|調整中|審査中|協議中|確認中"),
    ("FORECAST", r"見通し|予想|予報|見込み|推計"),
    ("PLANNED", r"予定|計画|開催します|実施します|募集します|公募します"),
    ("DECIDED", r"決定|承認|認定|閣議決定|公布"),
    ("CURRENT", r"現在|現時点|引き続き|継続して"),
)
NEGATION_RE = re.compile(r"ない|ません|ず|ぬ|禁止|不可|停止|中止")
MODALITY_RE = re.compile(r"予定|計画|検討|可能|見込み|方針|必要|義務|原則|決定")
DATE_RE = re.compile(r"令和[元0-9０-９]+年[0-9０-９]+月(?:[0-9０-９]+日)?|20[0-9]{2}年[0-9]{1,2}月(?:[0-9]{1,2}日)?|[0-9]{4}-[0-9]{2}-[0-9]{2}")
QUOTE_RE = re.compile(r"「[^」]{1,200}」|『[^』]{1,200}』|\"[^\"]{1,200}\"")
ATTRIBUTION_RE = re.compile(r"([一-龥ぁ-んァ-ヶA-Za-z0-9・]{2,40})(?:によると|は|が)(?:、|\s)*(?:次のとおり|以下のとおり|と発表|と説明|と述べ|と報告)")
CONDITION_RE = re.compile(r"場合|とき|限り|において|については")

@dataclass(frozen=True)
class TextChunk:
    index: int
    text: str
    start_char: int
    end_char: int
    start_byte: int
    end_byte: int

@dataclass(frozen=True)
class ClaimApplicability:
    proposition: str
    polarity: str
    topic: str = ""
    conditions: tuple[str, ...] = ()
    geography: str = ""
    temporal_state: str = "UNKNOWN"
    valid_from: str = ""
    valid_to: str = ""
    modality: str = "ASSERTED"

@dataclass(frozen=True)
class ApplicabilityResult:
    same_proposition: bool
    scopes_overlap: bool
    conflict: bool
    reason: str

def canonical_url(url: str) -> str:
    p = urlsplit(url.strip())
    if p.scheme not in ("http", "https") or not p.netloc:
        raise ValueError("SOURCE_URL_INVALID")
    return urlunsplit((p.scheme.lower(), p.netloc.lower(), p.path or "/", "", ""))

def temporal_state(text: str) -> str:
    for state, pattern in TEMPORAL_PATTERNS:
        if re.search(pattern, text):
            return state
    return "UNKNOWN"

def _normal(s: str) -> str:
    s = unicodedata.normalize("NFKC", s).casefold()
    return re.sub(r"[\s、。，．,.・:：;；「」『』（）()\[\]【】]+", "", s)

def _date_interval(a: ClaimApplicability) -> tuple[str, str]:
    return a.valid_from or "", a.valid_to or ""

class ClaimApplicabilityComparator:
    """Only reports contradiction when proposition AND applicable scope overlap."""
    @staticmethod
    def compare(a: ClaimApplicability, b: ClaimApplicability) -> ApplicabilityResult:
        same = _normal(a.proposition) == _normal(b.proposition)
        if not same:
            return ApplicabilityResult(False, False, False, "PROPOSITION_DIFFERENT")
        if a.topic and b.topic and _normal(a.topic) != _normal(b.topic):
            return ApplicabilityResult(True, False, False, "TOPIC_SCOPE_DISJOINT")
        if a.geography and b.geography and _normal(a.geography) != _normal(b.geography):
            return ApplicabilityResult(True, False, False, "GEOGRAPHY_SCOPE_DISJOINT")
        if a.conditions and b.conditions:
            left, right = {_normal(x) for x in a.conditions}, {_normal(x) for x in b.conditions}
            if left.isdisjoint(right):
                return ApplicabilityResult(True, False, False, "CONDITION_SCOPE_DISJOINT")
        af, at = _date_interval(a)
        bf, bt = _date_interval(b)
        if (at and bf and at < bf) or (bt and af and bt < af):
            return ApplicabilityResult(True, False, False, "TIME_INTERVAL_DISJOINT")
        if a.temporal_state == "CANCELLED" and b.temporal_state in ("PLANNED", "FORECAST"):
            return ApplicabilityResult(True, False, False, "LIFECYCLE_TRANSITION_NOT_CONTRADICTION")
        if b.temporal_state == "CANCELLED" and a.temporal_state in ("PLANNED", "FORECAST"):
            return ApplicabilityResult(True, False, False, "LIFECYCLE_TRANSITION_NOT_CONTRADICTION")
        if a.modality != b.modality and "ASSERTED" not in (a.modality, b.modality):
            return ApplicabilityResult(True, False, False, "MODALITY_SCOPE_DIFFERENT")
        opposite = a.polarity != b.polarity and "UNKNOWN" not in (a.polarity, b.polarity)
        return ApplicabilityResult(True, True, opposite,
            "OVERLAPPING_OPPOSITE_POLARITY" if opposite else "OVERLAPPING_SCOPE")

def split_document(text: str, max_chars: int = 6000, overlap_chars: int = 240) -> list[TextChunk]:
    if max_chars < 256 or overlap_chars < 0 or overlap_chars >= max_chars:
        raise ValueError("CHUNK_LIMIT_INVALID")
    normalized = unicodedata.normalize("NFC", text)
    if not normalized.strip():
        raise ValueError("DOCUMENT_TEXT_EMPTY")
    chunks: list[TextChunk] = []
    start = 0
    while start < len(normalized):
        hard_end = min(len(normalized), start + max_chars)
        end = hard_end
        if hard_end < len(normalized):
            floor = start + max_chars // 2
            candidates = [normalized.rfind(mark, floor, hard_end) for mark in ("。", "\n", "！", "？", ".", ";")]
            boundary = max(candidates)
            if boundary >= floor:
                end = boundary + 1
        part = normalized[start:end]
        bstart = len(normalized[:start].encode("utf-8"))
        bend = bstart + len(part.encode("utf-8"))
        chunks.append(TextChunk(len(chunks), part, start, end, bstart, bend))
        if end == len(normalized):
            break
        start = max(start + 1, end - overlap_chars)
    return chunks

def _byte_offset(text: str, char_offset: int) -> int:
    return len(text[:char_offset].encode("utf-8"))

def _marker_spans(text: str, base_char: int, source: str, digest: str,
                  packet_id: str, claim_id: str, index: int) -> list[dict[str, Any]]:
    spans: list[dict[str, Any]] = []
    def add(kind: str, match: re.Match[str], role: str = "", modality: str = "",
            closer: re.Match[str] | None = None, attribution_ref: str = ""):
        start_char = base_char + match.start()
        end_char = base_char + match.end()
        start_byte, end_byte = _byte_offset(source, start_char), _byte_offset(source, end_char)
        opener = {"anchor_id": f"anchor/{packet_id}/{index}/{len(spans)}/o",
            "source_digest": digest, "offset_basis": "UTF8_BYTE_NFC",
            "start_offset": start_byte, "end_offset": end_byte,
            "exact_text": source[start_char:end_char], "packet_ref": packet_id}
        closer_value = None
        if closer:
            cs, ce = base_char + closer.start(), base_char + closer.end()
            closer_value = {"anchor_id": f"anchor/{packet_id}/{index}/{len(spans)}/c",
                "source_digest": digest, "offset_basis": "UTF8_BYTE_NFC",
                "start_offset": _byte_offset(source, cs), "end_offset": _byte_offset(source, ce),
                "exact_text": source[cs:ce], "packet_ref": packet_id}
        spans.append({"span_id": f"scope/{packet_id}/{index}/{len(spans)}",
            "kind": kind, "source_digest": digest, "offset_basis": "UTF8_BYTE_NFC",
            "opener": opener, "closer": closer_value,
            "target_refs": [claim_id] if kind in ("NEGATION", "MODALITY", "TEMPORAL") else [],
            "antecedent_refs": [], "attribution_ref": attribution_ref,
            "modality_kind": modality, "temporal_role": role,
            "parent_ref": "", "packet_refs": [packet_id]})
    for m in NEGATION_RE.finditer(text):
        add("NEGATION", m)
    for m in MODALITY_RE.finditer(text):
        add("MODALITY", m, modality="PROSPECTIVE_OR_MODAL")
    state = temporal_state(text)
    dates = list(DATE_RE.finditer(text))
    if dates:
        add("TEMPORAL", dates[0], role=state)
    elif state != "UNKNOWN":
        state_match = next((m for _, pat in TEMPORAL_PATTERNS if (m := re.search(pat, text))), None)
        if state_match:
            add("TEMPORAL", state_match, role=state)
    for q in QUOTE_RE.finditer(text):
        quote = q.group(0)
        opener_char = q.start()
        opener_match = re.match(r"「|『|\"", quote)
        closer_match = re.search(r"」|』|\"", quote)
        if not opener_match or not closer_match:
            continue
        class _M:
            def __init__(self, s, e): self._s, self._e = s, e
            def start(self): return self._s
            def end(self): return self._e
        add("QUOTATION", _M(opener_char, opener_char + 1), closer=_M(q.end()-1, q.end()))
    return spans

class WebDocumentIndex:
    """Rebuildable SQLite index for URL revisions and cross-document claim identity."""
    def __init__(self, path: str | Path):
        self.db = sqlite3.connect(str(path))
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("""CREATE TABLE IF NOT EXISTS documents(
            url TEXT PRIMARY KEY, digest TEXT NOT NULL, packet_id TEXT NOT NULL,
            revision INTEGER NOT NULL, title TEXT NOT NULL, publisher TEXT NOT NULL)""")
        self.db.execute("""CREATE TABLE IF NOT EXISTS claims(
            signature TEXT PRIMARY KEY, claim_id TEXT NOT NULL, payload TEXT NOT NULL)""")
        self.db.commit()

    def inspect(self, url: str, digest: str) -> dict[str, Any]:
        row = self.db.execute("SELECT digest,packet_id,revision,title,publisher FROM documents WHERE url=?",
                              (url,)).fetchone()
        if not row:
            return {"status": "NEW", "previous_packet_id": "", "revision": 0}
        if row[0] == digest:
            return {"status": "DUPLICATE", "previous_packet_id": row[1], "revision": row[2]}
        return {"status": "UPDATE", "previous_packet_id": row[1], "revision": row[2] + 1}

    def find_claim(self, signature: str) -> dict[str, Any] | None:
        row = self.db.execute("SELECT claim_id,payload FROM claims WHERE signature=?", (signature,)).fetchone()
        return {"claim_id": row[0], **json.loads(row[1])} if row else None

    def record(self, url: str, digest: str, packet_id: str, revision: int,
               title: str, publisher: str, claims: Iterable[dict[str, Any]]) -> None:
        self.db.execute("""INSERT INTO documents(url,digest,packet_id,revision,title,publisher)
            VALUES(?,?,?,?,?,?) ON CONFLICT(url) DO UPDATE SET digest=excluded.digest,
            packet_id=excluded.packet_id,revision=excluded.revision,title=excluded.title,
            publisher=excluded.publisher""", (url,digest,packet_id,revision,title,publisher))
        for item in claims:
            self.db.execute("INSERT OR IGNORE INTO claims(signature,claim_id,payload) VALUES(?,?,?)",
                (item["signature"], item["claim_id"], json.dumps(item,ensure_ascii=False,sort_keys=True)))
        self.db.commit()

    def close(self) -> None:
        self.db.close()

class WebDocumentRecognitionEngine:
    """Web text -> chunk claims/scopes/provenance -> atomic Core admission."""
    def __init__(self, provider: Any, context_id: str, source_id: str,
                 index: WebDocumentIndex, max_chunk_chars: int = 6000,
                 overlap_chars: int = 240):
        self.recognizer = LLMRecognizer(provider)
        self.context_id, self.source_id = context_id, source_id
        self.index = index
        self.max_chunk_chars, self.overlap_chars = max_chunk_chars, overlap_chars
        self.packet_adapter = RecognitionPacketAdapter()
        self.sequence = 0

    def prepare(self, text: str, url: str, publisher: str, title: str = "",
                retrieved_at: str = "", context: dict[str, Any] | None = None) -> dict[str, Any]:
        source_text = unicodedata.normalize("NFC", text)
        source = normalize_source(source_text)
        digest = hashlib.sha256(source).hexdigest()
        url = canonical_url(url)
        identity = self.index.inspect(url, digest)
        if identity["status"] == "DUPLICATE":
            return {"status": "DUPLICATE", "url": url, "digest": digest,
                    "previous_packet_id": identity["previous_packet_id"]}
        self.sequence += 1
        packet_id = "web-" + hashlib.sha256((url + "|" + digest).encode()).hexdigest()[:24]
        packet = LLMSemanticRuntime._packet(source_text, self.context_id, self.source_id,
                                             "web-corpus-v1", self.sequence)
        packet["packet_id"] = packet_id
        packet["idempotency_key"] = "idem/" + packet_id
        packet["source_id"] = url
        packet["source_position"]["block_ref"] = "document/" + hashlib.sha256(url.encode()).hexdigest()[:16]
        packet["source_position"]["end_offset"] = len(source)
        packet["source_position"]["source_range_digest"] = digest
        packet["content"]["normalized_digest"] = digest
        packet["content"]["text"] = source_text
        packet["context_hint"]["retrieval_reason"] = "web-document-recognition-v0.1"
        if identity["status"] == "UPDATE":
            packet["finalization"] = "REVISION"
            packet["source_position"]["source_revision_sequence"] = identity["revision"]
            packet["source_position"]["revision_of_ref"] = identity["previous_packet_id"]
        else:
            packet["finalization"] = "FINAL"
        publisher = publisher.strip() or urlsplit(url).netloc
        publisher_id = "entity/publisher/" + hashlib.sha256(publisher.encode()).hexdigest()[:16]
        nodes: list[dict[str, Any]] = [{"id": publisher_id, "kind": "ENTITY",
            "content": json.dumps({"name": publisher, "role": "PUBLISHER"},ensure_ascii=False),
            "references": []}]
        relations: list[dict[str, str]] = []
        scopes: list[dict[str, Any]] = []
        extracted: list[dict[str, Any]] = []
        chunks = split_document(source_text, self.max_chunk_chars, self.overlap_chars)
        prior_claims: list[tuple[ClaimApplicability, str]] = []
        for chunk in chunks:
            candidate = self.recognizer.recognize(chunk.text, context or {})
            evidence_id = f"evidence/{packet_id}/{chunk.index}"
            evidence_content = {"url":url,"title":title,"publisher":publisher,
                "retrieved_at":retrieved_at,"source_digest":digest,
                "start_byte":chunk.start_byte,"end_byte":chunk.end_byte,
                "capture":"FULL_TEXT","chunk_index":chunk.index}
            nodes.append({"id":evidence_id,"kind":"EVIDENCE",
                "content":json.dumps(evidence_content,ensure_ascii=False,sort_keys=True),
                "references":[publisher_id]})
            if candidate["kind"] != "CLAIM" or not candidate["proposition"]:
                continue
            state = temporal_state(chunk.text)
            modality = "CONDITIONAL" if candidate["conditional"] else (
                "MODAL" if MODALITY_RE.search(chunk.text) else "ASSERTED")
            conditions = tuple(m.group(0) for m in CONDITION_RE.finditer(chunk.text))
            applicability = ClaimApplicability(candidate["proposition"],candidate["polarity"],
                candidate.get("topic") or "",conditions, "", state, "", "", modality)
            signature = hashlib.sha256(json.dumps({
                "proposition":_normal(applicability.proposition),
                "polarity":applicability.polarity,"topic":_normal(applicability.topic),
                "conditions":sorted(_normal(x) for x in conditions),
                "state":state,"modality":modality},ensure_ascii=False,sort_keys=True).encode()).hexdigest()
            old = self.index.find_claim(signature)
            claim_id = f"claim/{packet_id}/{chunk.index}"
            claim_content = {"proposition":candidate["proposition"],"polarity":candidate["polarity"],
                "topic":candidate.get("topic") or "", "conditional":candidate["conditional"],
                "temporal_state":state,"modality":modality,"conditions":list(conditions),
                "source_url":url,"publisher":publisher,"retrieved_at":retrieved_at,
                "source_digest":digest,"chunk_index":chunk.index,
                "supersedes_claim_id":old["claim_id"] if identity["status"]=="UPDATE" and old else ""}
            nodes.append({"id":claim_id,"kind":"CLAIM","content":json.dumps(
                claim_content,ensure_ascii=False,sort_keys=True),"references":[evidence_id]})
            cluster_id = None
            if candidate.get("topic"):
                cluster_id = "cluster/web/" + hashlib.sha256(_normal(candidate["topic"]).encode()).hexdigest()[:16]
                if not any(n["id"]==cluster_id for n in nodes):
                    nodes.append({"id":cluster_id,"kind":"CLUSTER","content":candidate["topic"],"references":[]})
                relations.append({"id":f"about/{packet_id}/{chunk.index}","kind":"ABOUT",
                    "from":claim_id,"to":cluster_id})
            if old and old["claim_id"] != claim_id:
                relations.append({"id":f"supports/{packet_id}/{chunk.index}","kind":"SUPPORTS",
                    "from":claim_id,"to":old["claim_id"]})
            for previous_app, previous_id in prior_claims:
                comparison = ClaimApplicabilityComparator.compare(previous_app,applicability)
                if comparison.conflict:
                    relations.append({"id":f"contradicts/{previous_id}/{claim_id}",
                        "kind":"CONTRADICTS","from":previous_id,"to":claim_id})
            prior_claims.append((applicability,claim_id))
            scopes.extend(_marker_spans(chunk.text,chunk.start_char,source_text,digest,
                                         packet_id,claim_id,chunk.index))
            extracted.append({"claim_id":claim_id,"signature":signature,
                "applicability":asdict(applicability),"chunk_index":chunk.index})
        # Explicit quotation attribution is separate from publisher attribution.
        for m in ATTRIBUTION_RE.finditer(source_text):
            quoted_party=m.group(1)
            entity_id="entity/attribution/"+hashlib.sha256(quoted_party.encode()).hexdigest()[:16]
            if not any(n["id"]==entity_id for n in nodes):
                nodes.append({"id":entity_id,"kind":"ENTITY","content":json.dumps(
                    {"name":quoted_party,"role":"ATTRIBUTED_SPEAKER"},ensure_ascii=False),"references":[]})
        delta={"delta_id":"semantic/"+packet_id,"idempotency_key":"semantic-idem/"+packet_id,
            "expected_state_version":0,"nodes":nodes,"relations":relations,
            "scope_spans":scopes,"source_snapshot":source_text}
        return {"status":identity["status"],"url":url,"digest":digest,"packet":packet,
            "delta":delta,"claims":extracted,"chunk_count":len(chunks),
            "previous_packet_id":identity["previous_packet_id"],"revision":identity["revision"]}

    def ingest(self, text: str, url: str, publisher: str, core: Any,
               title: str = "", retrieved_at: str = "", context: dict[str, Any] | None = None):
        prepared=self.prepare(text,url,publisher,title,retrieved_at,context)
        if prepared["status"]=="DUPLICATE":
            return prepared
        prepared["packet"]["context_hint"]["state_version"]=core.state_version
        prepared["delta"]["expected_state_version"]=core.state_version
        admission,commit=self.packet_adapter.admit_and_apply_to_core(
            prepared["packet"],text,core,prepared["delta"])
        self.index.record(prepared["url"],prepared["digest"],prepared["packet"]["packet_id"],
            prepared["revision"],title,publisher,prepared["claims"])
        return {**prepared,"admission":admission.status,"semantic_commit":commit}
