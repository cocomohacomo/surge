"""Automatic persistence gate for resolved RecognitionPacket temporal proposals.
Only explicitly bound, resolved candidates are eligible; this never writes ClaimValidity.
"""
from __future__ import annotations
from typing import Any

def persist_temporal_binding(core: Any, binding: dict[str, Any], *, packet_id: str) -> dict[str, Any]:
    """Persist one eligible binding with deterministic retry identifiers."""
    if not isinstance(packet_id, str) or not packet_id.strip():
        return {"status": "PACKET_ID_REQUIRED", "persisted": False, "claim_validity_write": False}
    if not isinstance(binding, dict):
        return {"status": "BINDING_INVALID", "persisted": False, "claim_validity_write": False}
    if binding.get("binding_status") != "BOUND_PENDING_CORE_REVIEW":
        return {"status": binding.get("binding_status", "BINDING_NOT_ELIGIBLE"),
                "persisted": False, "claim_validity_write": False}
    if binding.get("authority") != "PROPOSAL_ONLY" or binding.get("claim_validity_write") is not False:
        return {"status": "BINDING_AUTHORITY_INVALID", "persisted": False, "claim_validity_write": False}
    claim_id, evidence_id = binding.get("claim_ref"), binding.get("evidence_ref")
    candidate = binding.get("temporal_candidate")
    if (not isinstance(claim_id, str) or not claim_id.strip() or
            not isinstance(evidence_id, str) or not evidence_id.strip() or
            not isinstance(candidate, dict)):
        return {"status": "BINDING_PROVENANCE_INVALID", "persisted": False, "claim_validity_write": False}
    if candidate.get("status") != "RESOLVED" or candidate.get("correction"):
        return {"status": "CANDIDATE_NOT_PERSISTABLE", "persisted": False, "claim_validity_write": False}
    role = candidate.get("role")
    if role not in {"VALID_FROM", "VALID_UNTIL", "INTERVAL"}:
        return {"status": "UNSUPPORTED_TEMPORAL_ROLE", "persisted": False, "claim_validity_write": False}
    valid_from, valid_until = candidate.get("valid_from") or "", candidate.get("valid_until") or ""
    if ((role == "VALID_FROM" and (not valid_from or valid_until)) or
            (role == "VALID_UNTIL" and (valid_from or not valid_until)) or
            (role == "INTERVAL" and (not valid_from or not valid_until))):
        return {"status": "CANDIDATE_ROLE_BOUNDARY_MISMATCH", "persisted": False, "claim_validity_write": False}
    suffix = packet_id + "/" + role
    result = core.add_temporal_boundary(
        transaction_id="temporal-tx/" + suffix,
        idempotency_key="temporal-idem/" + suffix,
        event_id="temporal-event/" + suffix,
        expected_state_version=core.state_version,
        claim_id=claim_id, role=role, valid_from=valid_from, valid_until=valid_until,
        evidence_id=evidence_id, packet_id=packet_id)
    return {"status": result.get("status", "PERSISTED"),
            "persisted": bool(result.get("committed")),
            "duplicate": bool(result.get("duplicate")),
            "state_version": result.get("state_version"),
            "claim_ref": claim_id, "evidence_ref": evidence_id,
            "packet_ref": packet_id, "candidate_role": role,
            "pending": result.get("pending"), "claim_validity_write": False,
            "authority": "PROPOSAL_ONLY"}
