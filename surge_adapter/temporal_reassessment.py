"""Explainable point-in-time Claim reassessment with Evidence temporal validity.

The evaluator is read-only. It creates immutable, content-addressed traces and
an append-only hash chain; Core remains authoritative for graph mutations.
"""
from __future__ import annotations

from dataclasses import dataclass, asdict
from datetime import datetime, timezone
import hashlib
import json
from typing import Any, Iterable, Mapping

GENESIS = "0" * 64
RELATIONS = {"SUPPORTS", "CONTRADICTS", "RETRACTS"}
LIFECYCLE = {"ACTIVE", "SUPERSEDED", "RETRACTED"}


def _instant(value: Any, field: str, *, optional: bool = False) -> datetime | None:
    if value is None or value == "":
        if optional:
            return None
        raise ValueError("TEMPORAL_TRACE_TIMESTAMP_REQUIRED:" + field)
    if not isinstance(value, str):
        raise ValueError("TEMPORAL_TRACE_TIMESTAMP_INVALID:" + field)
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError("TEMPORAL_TRACE_TIMESTAMP_INVALID:" + field) from exc
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise ValueError("TEMPORAL_TRACE_TIMEZONE_REQUIRED:" + field)
    return parsed.astimezone(timezone.utc)


def _canonical(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


@dataclass(frozen=True)
class TemporalAssessment:
    status: str
    reference_at: str
    valid_from: str | None
    valid_until: str | None
    reason: str


def assess_interval(valid_from: Any, valid_until: Any, reference_at: Any,
                    *, field: str = "interval") -> TemporalAssessment:
    """Evaluate a half-open interval; missing bounds mean UNKNOWN, never VALID."""
    ref = _instant(reference_at, "reference_at")
    start = _instant(valid_from, field + ".valid_from", optional=True)
    end = _instant(valid_until, field + ".valid_until", optional=True)
    if start is None:
        return TemporalAssessment("UNKNOWN", ref.isoformat(), None,
                                 end.isoformat() if end else None,
                                 "VALID_FROM_MISSING")
    if end is not None and end <= start:
        raise ValueError("TEMPORAL_TRACE_INTERVAL_INVALID:" + field)
    if ref < start:
        status, reason = "NOT_YET_VALID", "REFERENCE_BEFORE_VALID_FROM"
    elif end is not None and ref >= end:
        status, reason = "EXPIRED", "REFERENCE_AT_OR_AFTER_EXCLUSIVE_VALID_UNTIL"
    else:
        status, reason = "VALID", "REFERENCE_WITHIN_HALF_OPEN_INTERVAL"
    return TemporalAssessment(status, ref.isoformat(), start.isoformat(),
                              end.isoformat() if end else None, reason)


def evaluate_claim_reassessment(claim: Mapping[str, Any],
                                evidence: Iterable[Mapping[str, Any]],
                                *, reference_at: str) -> dict[str, Any]:
    """Create a full explanation snapshot for one Claim at an explicit instant.

    Each Evidence item requires evidence_id, relation, valid_from and optionally
    valid_until. observed_at/source_published_at/source_retrieved_at are kept
    separately as provenance timestamps and are not confused with valid time.
    """
    if not isinstance(claim, Mapping):
        raise TypeError("claim must be a mapping")
    claim_id = claim.get("claim_id")
    revision = claim.get("revision")
    lifecycle = claim.get("claim_status", "ACTIVE")
    if not isinstance(claim_id, str) or not claim_id.strip():
        raise ValueError("TEMPORAL_TRACE_CLAIM_ID_REQUIRED")
    if isinstance(revision, bool) or not isinstance(revision, int) or revision < 0:
        raise ValueError("TEMPORAL_TRACE_CLAIM_REVISION_INVALID")
    if lifecycle not in LIFECYCLE:
        raise ValueError("TEMPORAL_TRACE_CLAIM_LIFECYCLE_INVALID")
    ref = _instant(reference_at, "reference_at")
    claim_assessment = assess_interval(claim.get("valid_from"), claim.get("valid_until"),
                                       reference_at, field="claim")
    evidence_rows: list[dict[str, Any]] = []
    for item in evidence:
        if not isinstance(item, Mapping):
            raise TypeError("evidence item must be a mapping")
        evidence_id, relation = item.get("evidence_id"), item.get("relation")
        if not isinstance(evidence_id, str) or not evidence_id.strip():
            raise ValueError("TEMPORAL_TRACE_EVIDENCE_ID_REQUIRED")
        if relation not in RELATIONS:
            raise ValueError("TEMPORAL_TRACE_EVIDENCE_RELATION_INVALID")
        assessment = assess_interval(item.get("valid_from"), item.get("valid_until"),
                                     reference_at, field="evidence")
        provenance = {}
        for key in ("observed_at", "source_published_at", "source_retrieved_at"):
            parsed = _instant(item.get(key), "evidence." + key, optional=True)
            provenance[key] = parsed.isoformat() if parsed else None
        evidence_rows.append({
            "evidence_id": evidence_id,
            "relation": relation,
            "temporal_status": assessment.status,
            "valid_from": assessment.valid_from,
            "valid_until": assessment.valid_until,
            "temporal_reason": assessment.reason,
            "observed_at": provenance["observed_at"],
            "source_published_at": provenance["source_published_at"],
            "source_retrieved_at": provenance["source_retrieved_at"],
            "recorded_state_version": item.get("recorded_state_version"),
            "eligible_at_reference": assessment.status == "VALID",
        })
    evidence_rows.sort(key=lambda row: (row["evidence_id"], row["relation"]))
    active_support = [x for x in evidence_rows if x["relation"] == "SUPPORTS"
                      and x["eligible_at_reference"]]
    active_contradiction = [x for x in evidence_rows if x["relation"] in {"CONTRADICTS", "RETRACTS"}
                            and x["eligible_at_reference"]]
    if lifecycle != "ACTIVE":
        decision, reasons = "CLAIM_NOT_ACTIVE", ["CLAIM_LIFECYCLE_" + lifecycle]
    elif claim_assessment.status != "VALID":
        decision, reasons = "CLAIM_NOT_CURRENTLY_VALID", ["CLAIM_TEMPORAL_" + claim_assessment.status]
    elif active_support and active_contradiction:
        decision, reasons = "TEMPORAL_CONFLICT", ["ACTIVE_SUPPORT_AND_CONTRADICTION"]
    elif active_contradiction:
        decision, reasons = "CONTRADICTED", ["ACTIVE_CONTRADICTION_OR_RETRACTION"]
    elif active_support:
        decision, reasons = "SUPPORTED", ["AT_LEAST_ONE_TEMPORALLY_VALID_SUPPORT"]
    elif evidence_rows:
        decision, reasons = "INSUFFICIENT_TEMPORAL_PROVENANCE", ["NO_EVIDENCE_VALID_AT_REFERENCE"]
    else:
        decision, reasons = "NO_EVIDENCE", ["EVIDENCE_SET_EMPTY"]
    snapshot = {
        "schema_version": "surge.temporal-reassessment-trace.v1",
        "claim_id": claim_id,
        "claim_revision": revision,
        "claim_lifecycle": lifecycle,
        "claim_recorded_state_version": claim.get("recorded_state_version"),
        "reference_at": ref.isoformat(),
        "claim_temporal": asdict(claim_assessment),
        "evidence": evidence_rows,
        "decision": decision,
        "decision_reasons": reasons,
        "active_support_evidence_ids": [x["evidence_id"] for x in active_support],
        "active_contradiction_evidence_ids": [x["evidence_id"] for x in active_contradiction],
        "authority": "READ_ONLY_EXPLANATION",
        "core_mutation": False,
    }
    digest = hashlib.sha256(_canonical(snapshot).encode("utf-8")).hexdigest()
    return {**snapshot, "trace_id": "tr/" + digest, "snapshot_sha256": digest}


class TemporalReassessmentLedger:
    """Append-only, verifiable local trace chain; export for durable Core storage."""

    def __init__(self) -> None:
        self._records: list[dict[str, Any]] = []

    def append(self, trace: Mapping[str, Any]) -> dict[str, Any]:
        if not isinstance(trace, Mapping) or trace.get("authority") != "READ_ONLY_EXPLANATION":
            raise ValueError("TEMPORAL_TRACE_AUTHORITY_INVALID")
        record = dict(trace)
        if record.get("core_mutation") is not False:
            raise ValueError("TEMPORAL_TRACE_MUST_BE_READ_ONLY")
        if record.get("snapshot_sha256") != hashlib.sha256(
                _canonical({k: v for k, v in record.items()
                            if k not in {"trace_id", "snapshot_sha256", "sequence",
                                         "previous_hash", "chain_hash"}}).encode("utf-8")).hexdigest():
            raise ValueError("TEMPORAL_TRACE_SNAPSHOT_HASH_INVALID")
        record["sequence"] = len(self._records) + 1
        record["previous_hash"] = self._records[-1]["chain_hash"] if self._records else GENESIS
        payload = {k: v for k, v in record.items() if k != "chain_hash"}
        record["chain_hash"] = hashlib.sha256(_canonical(payload).encode("utf-8")).hexdigest()
        self._records.append(record)
        return dict(record)

    def records(self) -> list[dict[str, Any]]:
        return [dict(record) for record in self._records]

    @classmethod
    def replay(cls, records: Iterable[Mapping[str, Any]]) -> "TemporalReassessmentLedger":
        ledger = cls()
        for supplied in records:
            record = dict(supplied)
            expected_sequence = len(ledger._records) + 1
            expected_previous = ledger._records[-1]["chain_hash"] if ledger._records else GENESIS
            if record.get("sequence") != expected_sequence:
                raise ValueError("TEMPORAL_TRACE_SEQUENCE_INVALID")
            if record.get("previous_hash") != expected_previous:
                raise ValueError("TEMPORAL_TRACE_CHAIN_LINK_INVALID")
            chain_hash = record.pop("chain_hash", None)
            expected_hash = hashlib.sha256(_canonical(record).encode("utf-8")).hexdigest()
            if chain_hash != expected_hash:
                raise ValueError("TEMPORAL_TRACE_CHAIN_HASH_INVALID")
            record["chain_hash"] = chain_hash
            # append() independently checks the immutable snapshot digest.
            base = {k: v for k, v in record.items()
                    if k not in {"sequence", "previous_hash", "chain_hash"}}
            ledger.append(base)
        return ledger
