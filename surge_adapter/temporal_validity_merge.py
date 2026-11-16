"""Conservative composition of temporal boundary candidates with ClaimValidity.

This module produces proposals only. Core remains the sole authority that
validates approval, revision lineage, and durable writes.
"""
from __future__ import annotations

from datetime import datetime
from typing import Any


def _instant(value: Any, field: str) -> datetime | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise ValueError("TEMPORAL_BOUNDARY_INVALID:" + field)
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError("TEMPORAL_BOUNDARY_INVALID:" + field) from exc
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise ValueError("TEMPORAL_BOUNDARY_TIMEZONE_REQUIRED:" + field)
    return parsed


def propose_temporal_validity_merge(
    existing: dict[str, Any] | None,
    candidates: list[dict[str, Any]],
) -> dict[str, Any]:
    """Merge exactly one resolved boundary with the opposite stored boundary.

    Multiple candidates are never ranked or guessed. A partial interval is
    returned as NEEDS_OTHER_BOUNDARY and is not eligible for Core commit.
    """
    if not isinstance(candidates, list) or len(candidates) != 1:
        return {"status": "AMBIGUOUS_CANDIDATES" if candidates else "NO_CANDIDATE",
                "claim_validity_write": False}
    candidate = candidates[0]
    if not isinstance(candidate, dict) or candidate.get("status") != "RESOLVED":
        return {"status": "CANDIDATE_NOT_RESOLVED", "claim_validity_write": False}

    role = candidate.get("role")
    if role not in {"VALID_FROM", "VALID_UNTIL", "INTERVAL"}:
        return {"status": "UNSUPPORTED_TEMPORAL_ROLE", "claim_validity_write": False}

    current = existing or {}
    try:
        start = _instant(current.get("valid_from"), "existing.valid_from")
        end = _instant(current.get("valid_until"), "existing.valid_until")
        proposed_start = _instant(candidate.get("valid_from"), "candidate.valid_from")
        proposed_end = _instant(candidate.get("valid_until"), "candidate.valid_until")
    except ValueError as exc:
        return {"status": str(exc), "claim_validity_write": False}

    if role == "VALID_FROM":
        if proposed_start is None or proposed_end is not None:
            return {"status": "CANDIDATE_ROLE_BOUNDARY_MISMATCH",
                    "claim_validity_write": False}
        start = proposed_start
    elif role == "VALID_UNTIL":
        if proposed_end is None or proposed_start is not None:
            return {"status": "CANDIDATE_ROLE_BOUNDARY_MISMATCH",
                    "claim_validity_write": False}
        end = proposed_end
    else:
        if proposed_start is None or proposed_end is None:
            return {"status": "CANDIDATE_INTERVAL_INCOMPLETE",
                    "claim_validity_write": False}
        start, end = proposed_start, proposed_end

    if start is None or end is None:
        return {
            "status": "NEEDS_OTHER_BOUNDARY",
            "valid_from": start.isoformat() if start else None,
            "valid_until": end.isoformat() if end else None,
            "claim_validity_write": False,
        }
    if end <= start:
        return {"status": "INTERVAL_INVALID", "claim_validity_write": False}

    revision = current.get("revision", 0)
    if isinstance(revision, bool) or not isinstance(revision, int) or revision < 0:
        return {"status": "EXISTING_REVISION_INVALID", "claim_validity_write": False}
    return {
        "status": "MERGED_PROPOSAL",
        "valid_from": start.isoformat(),
        "valid_until": end.isoformat(),
        "expected_revision": revision,
        "proposed_revision": revision + 1,
        "claim_validity_write": False,
        "requires_core_review": True,
    }
