"""Bounded, proposal-only accumulation of temporal boundaries across packets.

Callers must explicitly supply the same Claim ID for every contribution.
This in-memory helper does not retrieve Claims, infer identity, or write Core.
"""
from __future__ import annotations

from typing import Any

from surge_adapter.temporal_validity_merge import propose_temporal_validity_merge


class CrossPacketTemporalAccumulator:
    def __init__(self, *, max_claims: int = 4096):
        if isinstance(max_claims, bool) or not isinstance(max_claims, int) or max_claims < 1:
            raise ValueError("TEMPORAL_ACCUMULATOR_LIMIT_INVALID")
        self.max_claims = max_claims
        self._pending: dict[str, dict[str, Any]] = {}

    def add(self, claim_id: str, candidate: dict[str, Any], *,
            evidence_id: str, packet_id: str) -> dict[str, Any]:
        if not isinstance(claim_id, str) or not claim_id.strip():
            return {"status": "CLAIM_ID_REQUIRED", "claim_validity_write": False}
        if not isinstance(evidence_id, str) or not evidence_id.strip() or not isinstance(packet_id, str) or not packet_id.strip():
            return {"status": "PROVENANCE_REQUIRED", "claim_validity_write": False}
        if not isinstance(candidate, dict) or candidate.get("status") != "RESOLVED":
            return {"status": "CANDIDATE_NOT_RESOLVED", "claim_validity_write": False}
        role = candidate.get("role")
        if role not in {"VALID_FROM", "VALID_UNTIL", "INTERVAL"}:
            return {"status": "UNSUPPORTED_TEMPORAL_ROLE", "claim_validity_write": False}
        if claim_id not in self._pending and len(self._pending) >= self.max_claims:
            return {"status": "ACCUMULATOR_CAPACITY_EXCEEDED", "claim_validity_write": False}

        record = self._pending.setdefault(claim_id, {
            "valid_from": None, "valid_until": None,
            "evidence_ids": [], "packet_ids": [], "candidate_roles": {},
        })
        if role == "INTERVAL":
            if record["valid_from"] is not None or record["valid_until"] is not None:
                return {"status": "BOUNDARY_CONFLICT_REVIEW_REQUIRED",
                        "claim_validity_write": False}
        value = candidate.get("valid_from" if role == "VALID_FROM" else "valid_until") if role != "INTERVAL" else None
        if role == "VALID_FROM":
            if value is None:
                return {"status": "CANDIDATE_ROLE_BOUNDARY_MISMATCH", "claim_validity_write": False}
            if record["valid_from"] is not None and record["valid_from"] != value:
                return {"status": "BOUNDARY_CONFLICT_REVIEW_REQUIRED", "claim_validity_write": False}
            record["valid_from"] = value
        elif role == "VALID_UNTIL":
            if value is None:
                return {"status": "CANDIDATE_ROLE_BOUNDARY_MISMATCH", "claim_validity_write": False}
            if record["valid_until"] is not None and record["valid_until"] != value:
                return {"status": "BOUNDARY_CONFLICT_REVIEW_REQUIRED", "claim_validity_write": False}
            record["valid_until"] = value
        else:
            start, end = candidate.get("valid_from"), candidate.get("valid_until")
            if not start or not end or record["valid_from"] is not None or record["valid_until"] is not None:
                return {"status": "CANDIDATE_INTERVAL_INCOMPLETE_OR_CONFLICTING",
                        "claim_validity_write": False}
            record["valid_from"], record["valid_until"] = start, end

        record["candidate_roles"][role] = True
        if evidence_id not in record["evidence_ids"]:
            record["evidence_ids"].append(evidence_id)
        if packet_id not in record["packet_ids"]:
            record["packet_ids"].append(packet_id)

        proposal_candidate = {
            "role": "INTERVAL", "status": "RESOLVED",
            "valid_from": record["valid_from"], "valid_until": record["valid_until"],
        }
        if not record["valid_from"] or not record["valid_until"]:
            return {
                "status": "NEEDS_OTHER_BOUNDARY",
                "claim_ref": claim_id,
                "valid_from": record["valid_from"],
                "valid_until": record["valid_until"],
                "basis_evidence_ids": list(record["evidence_ids"]),
                "source_packet_ids": list(record["packet_ids"]),
                "claim_validity_write": False,
            }
        merged = propose_temporal_validity_merge(
            None, [proposal_candidate])
        if merged["status"] != "MERGED_PROPOSAL":
            return {**merged, "claim_ref": claim_id,
                    "basis_evidence_ids": list(record["evidence_ids"]),
                    "source_packet_ids": list(record["packet_ids"])}
        return {
            **merged,
            "claim_ref": claim_id,
            "basis_evidence_ids": list(record["evidence_ids"]),
            "source_packet_ids": list(record["packet_ids"]),
            "status": "MERGED_PROPOSAL_PENDING_REVIEW",
            "claim_validity_write": False,
        }

    def pending_claim_count(self) -> int:
        return len(self._pending)

    def clear(self, claim_id: str) -> bool:
        """Explicitly clear a Claim's pending proposal after caller disposition."""
        return self._pending.pop(claim_id, None) is not None
