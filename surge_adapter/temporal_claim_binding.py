"""Conservative association of temporal cues with accepted Claim proposals.

This layer proposes a Claim/Evidence binding. It never writes ClaimValidity.
"""
from __future__ import annotations

import unicodedata
from typing import Any


def bind_temporal_candidates(
    source_text: str,
    temporal_spans: list[dict[str, Any]],
    *,
    claim_id: str | None,
    evidence_id: str,
    claim_accepted: bool,
) -> list[dict[str, Any]]:
    """Bind only a single resolved cue in a single-clause accepted Claim packet.

    Multi-cue, correction, unresolved, or multi-clause inputs are surfaced for
    review rather than guessed. Returned bindings are proposal metadata only.
    """
    normalized = unicodedata.normalize("NFC", source_text)
    raw = normalized.encode("utf-8")
    clauses = [part.strip() for part in __import__("re").split(r"[。！？!?；;、\n]+", normalized)
               if part.strip()]
    one_clause = len(clauses) == 1
    results: list[dict[str, Any]] = []
    for span in temporal_spans:
        candidate = span.get("candidate", {})
        status = candidate.get("status")
        correction = bool(candidate.get("correction"))
        if not claim_accepted or not claim_id:
            binding_status = "NO_ACCEPTED_CLAIM"
        elif correction:
            binding_status = "CORRECTION_REVIEW_REQUIRED"
        elif status != "RESOLVED":
            binding_status = "TEMPORAL_UNRESOLVED"
        elif len(temporal_spans) != 1:
            binding_status = "MULTIPLE_TEMPORAL_CUES"
        elif not one_clause:
            binding_status = "MULTI_CLAUSE_SCOPE_AMBIGUOUS"
        else:
            binding_status = "BOUND_PENDING_CORE_REVIEW"

        start, end = span.get("start_offset"), span.get("end_offset")
        if (not isinstance(start, int) or isinstance(start, bool) or
                not isinstance(end, int) or isinstance(end, bool) or
                start < 0 or end <= start or end > len(raw)):
            binding_status = "SOURCE_SPAN_INVALID"
        else:
            try:
                exact = raw[start:end].decode("utf-8")
            except UnicodeDecodeError:
                binding_status = "SOURCE_SPAN_INVALID"
                exact = None
            if exact is not None and exact != span.get("exact_text"):
                binding_status = "SOURCE_SPAN_MISMATCH"

        results.append({
            "binding_status": binding_status,
            "claim_ref": claim_id if claim_accepted else None,
            "evidence_ref": evidence_id,
            "source_span": {
                "start_offset": start, "end_offset": end,
                "offset_basis": "UTF8_BYTE_NFC",
                "exact_text": span.get("exact_text"),
            },
            "temporal_candidate": candidate,
            "binding_basis": (
                "SINGLE_ACCEPTED_CLAIM_SINGLE_RESOLVED_CUE_SINGLE_CLAUSE"
                if binding_status == "BOUND_PENDING_CORE_REVIEW" else "CONSERVATIVE_REVIEW_GATE"
            ),
            "authority": "PROPOSAL_ONLY",
            "claim_validity_write": False,
        })
    return results
