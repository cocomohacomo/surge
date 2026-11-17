"""Compact, replaceable local Recognition Engine boundary.

The backend is deliberately injected: this module does not bundle a model or
runtime. It verifies the model artifact and measures end-to-end inference RTF
against the source media duration supplied by the caller.
"""
from __future__ import annotations

import hashlib
import math
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Protocol, Sequence


DEFAULT_MODEL_LIMIT_BYTES = 30 * 1024 * 1024
DEFAULT_RTF_TARGET = 0.01


@dataclass(frozen=True)
class CompactModelSpec:
    model_id: str
    version: str
    artifact_path: str
    artifact_bytes: int
    sha256: str
    runtime_id: str
    device: str = "CPU"
    max_model_bytes: int = DEFAULT_MODEL_LIMIT_BYTES
    target_rtf: float = DEFAULT_RTF_TARGET

    @classmethod
    def inspect(cls, model_id: str, version: str, artifact_path: str, *,
                runtime_id: str, device: str = "CPU",
                max_model_bytes: int = DEFAULT_MODEL_LIMIT_BYTES,
                target_rtf: float = DEFAULT_RTF_TARGET) -> "CompactModelSpec":
        path = Path(artifact_path)
        if not path.is_file():
            raise ValueError("MODEL_ARTIFACT_NOT_FOUND")
        size = path.stat().st_size
        if size <= 0:
            raise ValueError("MODEL_ARTIFACT_EMPTY")
        if size > max_model_bytes:
            raise ValueError("MODEL_ARTIFACT_SIZE_LIMIT")
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if not model_id or not version or not runtime_id:
            raise ValueError("MODEL_DESCRIPTOR_REQUIRED")
        if device not in ("CPU", "GPU", "NPU", "AUTO"):
            raise ValueError("MODEL_DEVICE_INVALID")
        if isinstance(target_rtf, bool) or not math.isfinite(target_rtf) or target_rtf <= 0:
            raise ValueError("MODEL_RTF_TARGET_INVALID")
        return cls(model_id, version, str(path.resolve()), size, digest.hexdigest(),
                   runtime_id, device, max_model_bytes, float(target_rtf))


@dataclass(frozen=True)
class RecognitionCandidate:
    kind: str
    topic: str | None
    proposition: str | None
    polarity: str
    conditional: bool
    confidence: float
    ambiguity: float
    importance: float
    start_offset: int = 0
    end_offset: int = 0

    def __post_init__(self) -> None:
        if not self.kind or self.polarity not in ("POSITIVE", "NEGATIVE", "UNKNOWN"):
            raise ValueError("RECOGNITION_CANDIDATE_LABEL_INVALID")
        if not isinstance(self.conditional, bool):
            raise ValueError("RECOGNITION_CANDIDATE_CONDITIONAL_INVALID")
        for name in ("confidence", "ambiguity", "importance"):
            value = getattr(self, name)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= 1:
                raise ValueError("RECOGNITION_CANDIDATE_SCORE_INVALID:" + name)
        if self.start_offset < 0 or self.end_offset < self.start_offset:
            raise ValueError("RECOGNITION_CANDIDATE_OFFSET_INVALID")


class CompactModelBackend(Protocol):
    """Backend contract for ONNX Runtime, custom C/C++, or another local engine."""
    def recognize(self, text: str, context: dict[str, Any] | None = None) -> Sequence[RecognitionCandidate]: ...


@dataclass(frozen=True)
class RecognitionExecution:
    model_id: str
    model_bytes: int
    elapsed_seconds: float
    audio_duration_seconds: float | None
    rtf: float | None
    target_rtf: float
    performance_target_met: bool | None
    candidates: tuple[RecognitionCandidate, ...]

    def as_dict(self) -> dict[str, Any]:
        return {
            "model_id": self.model_id,
            "model_bytes": self.model_bytes,
            "elapsed_seconds": self.elapsed_seconds,
            "audio_duration_seconds": self.audio_duration_seconds,
            "rtf": self.rtf,
            "target_rtf": self.target_rtf,
            "performance_target_met": self.performance_target_met,
            "candidates": [
                {
                    "kind": c.kind, "topic": c.topic, "proposition": c.proposition,
                    "polarity": c.polarity, "conditional": c.conditional,
                    "confidence": c.confidence, "ambiguity": c.ambiguity,
                    "importance": c.importance, "start_offset": c.start_offset,
                    "end_offset": c.end_offset,
                } for c in self.candidates
            ],
        }


class CompactRecognitionRuntime:
    """Runs a verified compact model and returns proposals, never Core writes."""
    def __init__(self, spec: CompactModelSpec, backend: CompactModelBackend):
        self.spec = spec
        self.backend = backend

    def recognize(self, text: str, context: dict[str, Any] | None = None, *,
                  audio_duration_seconds: float | None = None) -> RecognitionExecution:
        if not isinstance(text, str) or not text.strip() or "\x00" in text:
            raise ValueError("RECOGNITION_INPUT_INVALID")
        if audio_duration_seconds is not None:
            if (isinstance(audio_duration_seconds, bool) or
                not isinstance(audio_duration_seconds, (int, float)) or
                not math.isfinite(audio_duration_seconds) or audio_duration_seconds <= 0):
                raise ValueError("AUDIO_DURATION_INVALID")
        started = time.perf_counter()
        candidates = tuple(self.backend.recognize(text, context or {}))
        elapsed = time.perf_counter() - started
        if not all(isinstance(item, RecognitionCandidate) for item in candidates):
            raise ValueError("BACKEND_CANDIDATE_TYPE_INVALID")
        rtf = elapsed / float(audio_duration_seconds) if audio_duration_seconds is not None else None
        return RecognitionExecution(
            self.spec.model_id, self.spec.artifact_bytes, elapsed,
            float(audio_duration_seconds) if audio_duration_seconds is not None else None,
            rtf, self.spec.target_rtf,
            (rtf < self.spec.target_rtf) if rtf is not None else None,
            candidates,
        )


def summarize_rtf(executions: Sequence[RecognitionExecution]) -> dict[str, Any]:
    """Aggregate measured executions; no synthetic timing or inferred duration."""
    measured = [item.rtf for item in executions if item.rtf is not None]
    if not measured:
        return {"sample_count": len(executions), "measured_count": 0,
                "target_rtf": executions[0].target_rtf if executions else DEFAULT_RTF_TARGET,
                "mean_rtf": None, "max_rtf": None, "target_pass": None}
    target = executions[0].target_rtf
    if any(item.target_rtf != target for item in executions):
        raise ValueError("BENCHMARK_TARGET_MISMATCH")
    return {
        "sample_count": len(executions), "measured_count": len(measured),
        "target_rtf": target, "mean_rtf": sum(measured) / len(measured),
        "max_rtf": max(measured), "target_pass": all(value < target for value in measured),
    }
