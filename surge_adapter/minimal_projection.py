"""Strict boundary validation for the external Projector's minimal output.

This module deliberately does not interpret distance, rank clusters, or mutate Core.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Any, Mapping

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "schemas" / "surge-minimal-projection-v0.1.schema.json"


class MinimalProjectionRejected(ValueError):
    """The external Projector output violates the minimal contract."""


@dataclass(frozen=True)
class MinimalProjection:
    information: str
    target_cluster: str | None
    distance: float

    def to_dict(self) -> dict[str, Any]:
        return {
            "information": self.information,
            "target_cluster": self.target_cluster,
            "distance": self.distance,
        }


class MinimalProjectionAdapter:
    """Validate and preserve Projector output without semantic reinterpretation."""

    def __init__(self, schema_path: str | Path = SCHEMA_PATH):
        schema = json.loads(Path(schema_path).read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        self._validator = Draft202012Validator(schema)

    def validate(self, value: Mapping[str, Any]) -> MinimalProjection:
        if not isinstance(value, Mapping):
            raise MinimalProjectionRejected("PROJECTION_MUST_BE_OBJECT")
        errors = sorted(
            self._validator.iter_errors(dict(value)),
            key=lambda error: (list(map(str, error.absolute_path)), error.message),
        )
        if errors:
            error = errors[0]
            path = "/".join(map(str, error.absolute_path)) or "$"
            raise MinimalProjectionRejected(f"SCHEMA_INVALID:{path}:{error.message}")
        distance = value["distance"]
        if isinstance(distance, bool) or not isinstance(distance, (int, float)):
            raise MinimalProjectionRejected("DISTANCE_MUST_BE_NUMBER")
        if not math.isfinite(distance):
            raise MinimalProjectionRejected("DISTANCE_MUST_BE_FINITE")
        information = value["information"]
        if not information.strip():
            raise MinimalProjectionRejected("INFORMATION_MUST_NOT_BE_BLANK")
        target = value["target_cluster"]
        if target is not None and not target.strip():
            raise MinimalProjectionRejected("TARGET_CLUSTER_MUST_BE_NONEMPTY_OR_NULL")
        return MinimalProjection(information, target, float(distance))
