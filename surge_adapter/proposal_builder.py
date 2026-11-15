"""Builds schema-validated, non-committing SURGE ClassificationProposal records."""
from __future__ import annotations
import json
import pathlib
from typing import Any
from jsonschema import Draft202012Validator, FormatChecker
from surge_adapter.recognition_scoring import SCORE_FIELDS, ScoringResult

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "schemas" / "surge-v0.9-proposal.schema.json"
ABSTENTION_MAP = {
    "LOW_CONFIDENCE": "LOW_MARGIN",
    "AMBIGUOUS_REFERENCE": "LOW_MARGIN",
    "LABEL_CONFLICT": "LABEL_CONFLICT",
    "OUT_OF_DISTRIBUTION": "OUT_OF_DISTRIBUTION",
    "UNSUPPORTED_LANGUAGE": "UNSUPPORTED_LANGUAGE",
    "MODEL_ERROR": "MODEL_ERROR",
}


def build_classification_proposal(
    packet: dict[str, Any], scoring: ScoringResult, *,
    proposal_id: str, producer_id: str, producer_version: str,
    model_descriptor: str, runtime_descriptor: str, base_state_version: int,
    candidate_labels: list[dict[str, str]],
    candidate_spans: list[dict[str, Any]] | None = None,
    schema_path: str | pathlib.Path = SCHEMA_PATH,
) -> dict[str, Any]:
    """Map typed candidates and score dimensions into the v0.9 proposal schema.

    This function does not call Core and cannot commit a SemanticDelta.
    Candidate span offsets are absolute half-open UTF8_BYTE_NFC packet offsets.
    """
    if not isinstance(base_state_version, int) or isinstance(base_state_version, bool) or base_state_version < 0:
        raise ValueError("BASE_STATE_VERSION_INVALID")
    try:
        pos = packet["source_position"]
        hint = packet["context_hint"]
        packet_id = packet["packet_id"]
        idempotency_key = packet["idempotency_key"]
    except (KeyError, TypeError) as exc:
        raise ValueError("PACKET_LINEAGE_FIELDS_REQUIRED") from exc

    labels = []
    for candidate in candidate_labels:
        dimension = candidate.get("score_dimension", "importance")
        if dimension not in SCORE_FIELDS:
            raise ValueError(f"UNKNOWN_SCORE_DIMENSION:{dimension}")
        item = {"family": candidate["family"], "label": candidate["label"],
                "score": scoring.scores[dimension], "score_scale": "0..1"}
        for optional in ("calibration_ref", "head_version"):
            if optional in candidate:
                item[optional] = candidate[optional]
        labels.append(item)

    spans = []
    for span in candidate_spans or []:
        start, end = span.get("start_offset"), span.get("end_offset")
        if (not isinstance(start, int) or isinstance(start, bool)
                or not isinstance(end, int) or isinstance(end, bool)
                or start < pos["start_offset"] or end <= start or end > pos["end_offset"]):
            raise ValueError("CANDIDATE_SPAN_OUT_OF_PACKET_RANGE")
        if span.get("offset_basis") != pos["offset_basis"]:
            raise ValueError("CANDIDATE_SPAN_OFFSET_BASIS_MISMATCH")
        spans.append(dict(span))

    abstentions = sorted({ABSTENTION_MAP[r] for r in scoring.abstention_reasons if r in ABSTENTION_MAP})
    warnings = [f"SCORING_DECISION:{scoring.decision}", f"SCORE_POLICY:{scoring.score_policy}"]
    warnings.extend(f"SCORING_DETAIL:{reason}" for reason in scoring.abstention_reasons)
    if scoring.decision == "REVIEW":
        warnings.append("MANUAL_REVIEW_REQUIRED")

    proposal = {
        "schema_version": "0.9.0", "proposal_id": proposal_id, "packet_ref": packet_id,
        "producer": {"producer_id": producer_id, "producer_version": producer_version},
        "model_descriptor": model_descriptor, "runtime_descriptor": runtime_descriptor,
        "base_state_version": base_state_version, "labels": labels,
        "candidate_spans": spans, "abstention_reasons": abstentions,
        "warnings": warnings, "idempotency_key": idempotency_key,
        "packet_source_generation": pos["source_generation"],
        "packet_source_sequence": pos["source_sequence"],
        "context_hint_state_version": hint["state_version"],
    }
    schema = json.loads(pathlib.Path(schema_path).read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    errors = sorted(Draft202012Validator(schema, format_checker=FormatChecker()).iter_errors(proposal),
                    key=lambda e: (list(map(str, e.absolute_path)), e.message))
    if errors:
        first = errors[0]
        path = "/".join(map(str, first.absolute_path)) or "$"
        raise ValueError(f"PROPOSAL_SCHEMA_INVALID:{path}:{first.message}")
    return proposal
