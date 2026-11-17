"""Explicit orchestration for compact recognition -> packet -> scoring.

Preparation is read-only. Durable packet admission is a separate explicit call.
"""
from __future__ import annotations
from dataclasses import dataclass
from typing import Any, Mapping, Protocol

from surge_adapter.compact_recognition import (
    CompactRecognitionRuntime, RecognitionPacketBuild, RecognitionPacketMetadata,
    RecognitionExecution, build_recognition_packet,
)
from surge_adapter.recognition_candidate_scoring import (
    CandidateAssessment, RecognitionCandidateScoringBridge, partition_assessments,
)


class PacketAdmissionPort(Protocol):
    def admit_to_core(self, packet: dict[str, Any], source_snapshot: str,
                      core: Any) -> Any: ...


@dataclass(frozen=True)
class PreparedRecognition:
    source_text: str
    execution: RecognitionExecution
    packet_build: RecognitionPacketBuild
    assessments: tuple[CandidateAssessment, ...]
    partitions: dict[str, Any]

    @property
    def accepted_count(self) -> int:
        return len(self.partitions["accepted"])

    @property
    def review_count(self) -> int:
        return len(self.partitions["review"])

    @property
    def abstained_count(self) -> int:
        return len(self.partitions["abstained"])

    @property
    def commit_ready(self) -> bool:
        return False


class CompactRecognitionPipeline:
    """Coordinates the replaceable recognizer, packet builder and scorer."""
    def __init__(self, runtime: CompactRecognitionRuntime,
                 scoring_bridge: RecognitionCandidateScoringBridge):
        self.runtime = runtime
        self.scoring_bridge = scoring_bridge

    def prepare(self, source_text: str, metadata: RecognitionPacketMetadata, *,
                context: Mapping[str, Any] | None = None,
                audio_duration_seconds: float | None = None) -> PreparedRecognition:
        execution = self.runtime.recognize(
            source_text, dict(context or {}),
            audio_duration_seconds=audio_duration_seconds,
        )
        packet_build = build_recognition_packet(source_text, execution, metadata)
        assessments = self.scoring_bridge.assess_batch(
            packet_build.proposal_batch,
            expected_packet_id=packet_build.packet["packet_id"],
            expected_source_digest=packet_build.packet["content"]["normalized_digest"],
            context=context,
        )
        partitions = partition_assessments(assessments)
        return PreparedRecognition(
            source_text, execution, packet_build, assessments, partitions,
        )

    @staticmethod
    def admit_packet(prepared: PreparedRecognition, adapter: PacketAdmissionPort,
                     core: Any) -> Any:
        """Explicitly admit source packet; does not commit semantic candidates."""
        return adapter.admit_to_core(
            prepared.packet_build.packet, prepared.source_text, core,
        )
