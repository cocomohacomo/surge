"""Auditable memory retention planning for Chaos Cluster projections.

This module plans archival and emits explicit tombstone records. It never
rewrites or truncates the authoritative EventJournal.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import pathlib
import tempfile
from datetime import datetime, timezone
from typing import Any, Iterable


MANIFEST_VERSION = 1
DEFAULT_WEIGHTS = {
    "salience": 0.30,
    "utility": 0.25,
    "interest": 0.20,
    "dependency": 0.15,
    "recency": 0.10,
}


def _utc(value: str, field: str) -> datetime:
    if not isinstance(value, str) or not value:
        raise ValueError(f"RETENTION_{field.upper()}_INVALID")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError(f"RETENTION_{field.upper()}_INVALID") from exc
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise ValueError(f"RETENTION_{field.upper()}_MUST_BE_AWARE")
    return parsed.astimezone(timezone.utc)


def _unit(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"RETENTION_{field.upper()}_INVALID")
    result = float(value)
    if not math.isfinite(result) or not 0.0 <= result <= 1.0:
        raise ValueError(f"RETENTION_{field.upper()}_OUT_OF_RANGE")
    return result


def _canonical(value: dict[str, Any]) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def _digest(value: dict[str, Any]) -> str:
    return hashlib.sha256(_canonical(value)).hexdigest()


def build_retention_manifest(
    records: Iterable[dict[str, Any]],
    *,
    capacity: int,
    policy_version: str,
    now: str,
    weights: dict[str, float] | None = None,
) -> dict[str, Any]:
    """Create a deterministic active/archive plan without deleting source data.

    Each record requires record_id and last_accessed_at. Optional factors default
    to zero. Coherence/organization is deliberately not an eviction factor.
    """
    if isinstance(capacity, bool) or not isinstance(capacity, int) or capacity < 0:
        raise ValueError("RETENTION_CAPACITY_INVALID")
    if not isinstance(policy_version, str) or not policy_version.strip():
        raise ValueError("RETENTION_POLICY_VERSION_INVALID")
    as_of = _utc(now, "now")
    chosen_weights = dict(DEFAULT_WEIGHTS if weights is None else weights)
    if set(chosen_weights) != set(DEFAULT_WEIGHTS):
        raise ValueError("RETENTION_WEIGHT_KEYS_INVALID")
    normalized_weights = {key: _unit(value, f"weight_{key}")
                           for key, value in chosen_weights.items()}
    if not math.isclose(sum(normalized_weights.values()), 1.0, abs_tol=1e-9):
        raise ValueError("RETENTION_WEIGHTS_MUST_SUM_TO_ONE")

    seen: set[str] = set()
    planned: list[dict[str, Any]] = []
    for source in records:
        if not isinstance(source, dict):
            raise ValueError("RETENTION_RECORD_INVALID")
        record_id = source.get("record_id")
        if not isinstance(record_id, str) or not record_id.strip() or record_id in seen:
            raise ValueError("RETENTION_RECORD_ID_INVALID_OR_DUPLICATE")
        seen.add(record_id)
        last_accessed = _utc(source.get("last_accessed_at"), "last_accessed_at")
        age_days = max(0.0, (as_of - last_accessed).total_seconds() / 86400.0)
        recency = math.exp(-age_days / 30.0)
        factors = {
            "salience": _unit(source.get("salience", 0.0), "salience"),
            "utility": _unit(source.get("utility", 0.0), "utility"),
            "interest": _unit(source.get("interest", 0.0), "interest"),
            "dependency": _unit(source.get("dependency", 0.0), "dependency"),
            "recency": recency,
        }
        score = sum(factors[key] * normalized_weights[key] for key in factors)
        planned.append({
            "record_id": record_id,
            "source_digest": source.get("source_digest"),
            "provenance_refs": sorted(set(source.get("provenance_refs", []))),
            "lineage_refs": sorted(set(source.get("lineage_refs", []))),
            "last_accessed_at": last_accessed.isoformat().replace("+00:00", "Z"),
            "retention_score": round(score, 12),
            "factors": {key: round(value, 12) for key, value in factors.items()},
        })

    # Stable ordering makes repeated planning with the same inputs reproducible.
    ranked = sorted(planned, key=lambda item: (-item["retention_score"], item["record_id"]))
    active_ids = {item["record_id"] for item in ranked[:capacity]}
    for item in planned:
        item["state"] = "ACTIVE" if item["record_id"] in active_ids else "ARCHIVED"
        item["decision_reason"] = (
            "WITHIN_CAPACITY" if item["state"] == "ACTIVE"
            else "CAPACITY_PRESSURE_LOWEST_RETENTION_SCORE"
        )
    body = {
        "manifest_version": MANIFEST_VERSION,
        "policy_version": policy_version,
        "created_at": as_of.isoformat().replace("+00:00", "Z"),
        "capacity": capacity,
        "weights": normalized_weights,
        "record_count": len(planned),
        "active_count": len(active_ids),
        "archived_count": len(planned) - len(active_ids),
        "records": sorted(planned, key=lambda item: item["record_id"]),
        "ranking": [item["record_id"] for item in ranked],
        "deletion_performed": False,
    }
    return {**body, "manifest_sha256": _digest(body)}


def verify_retention_manifest(manifest: dict[str, Any]) -> bool:
    if not isinstance(manifest, dict) or manifest.get("manifest_version") != MANIFEST_VERSION:
        raise ValueError("RETENTION_MANIFEST_VERSION_INVALID")
    body = {key: value for key, value in manifest.items() if key != "manifest_sha256"}
    if manifest.get("manifest_sha256") != _digest(body):
        raise ValueError("RETENTION_MANIFEST_DIGEST_MISMATCH")
    records = manifest.get("records")
    if not isinstance(records, list) or len(records) != manifest.get("record_count"):
        raise ValueError("RETENTION_MANIFEST_RECORD_COUNT_MISMATCH")
    ids = [item.get("record_id") for item in records if isinstance(item, dict)]
    if len(ids) != len(records) or len(ids) != len(set(ids)):
        raise ValueError("RETENTION_MANIFEST_RECORDS_INVALID")
    if any(item.get("state") not in {"ACTIVE", "ARCHIVED"} for item in records):
        raise ValueError("RETENTION_MANIFEST_STATE_INVALID")
    if manifest.get("deletion_performed") is not False:
        raise ValueError("RETENTION_MANIFEST_MUST_NOT_CLAIM_DELETION")
    return True


def create_payload_tombstone(
    record_id: str, *, manifest: dict[str, Any], reason: str,
    actor: str, policy_version: str, created_at: str,
) -> dict[str, Any]:
    """Emit an auditable tombstone proposal; caller persists it as a journal event.

    A tombstone is metadata, not proof that bytes were physically erased.
    """
    verify_retention_manifest(manifest)
    if not isinstance(record_id, str) or not record_id:
        raise ValueError("RETENTION_TOMBSTONE_RECORD_ID_INVALID")
    item = next((entry for entry in manifest["records"]
                 if entry["record_id"] == record_id), None)
    if item is None or item["state"] != "ARCHIVED":
        raise ValueError("RETENTION_TOMBSTONE_REQUIRES_ARCHIVED_RECORD")
    for value, code in ((reason, "REASON"), (actor, "ACTOR"),
                        (policy_version, "POLICY_VERSION")):
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f"RETENTION_TOMBSTONE_{code}_INVALID")
    timestamp = _utc(created_at, "created_at").isoformat().replace("+00:00", "Z")
    body = {
        "event_type": "PAYLOAD_TOMBSTONE_PROPOSED",
        "record_id": record_id,
        "manifest_sha256": manifest["manifest_sha256"],
        "source_digest": item.get("source_digest"),
        "reason": reason,
        "actor": actor,
        "policy_version": policy_version,
        "created_at": timestamp,
        "physical_deletion_performed": False,
    }
    return {**body, "event_sha256": _digest(body)}


def write_retention_manifest(path: pathlib.Path, manifest: dict[str, Any]) -> None:
    verify_retention_manifest(manifest)
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temp = pathlib.Path(name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(manifest, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, path)
        if os.name != "nt":
            directory_fd = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
    except Exception:
        temp.unlink(missing_ok=True)
        raise


def load_retention_manifest(path: pathlib.Path) -> dict[str, Any]:
    manifest = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    verify_retention_manifest(manifest)
    return manifest
