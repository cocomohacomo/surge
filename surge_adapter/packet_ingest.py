"""RecognitionPacket admission boundary.

This module validates packet shape and source-range integrity before a packet
is handed to the C++ Core. It deliberately does not perform semantic inference
or authorize a state commit.
"""
from __future__ import annotations

import hashlib
import json
import pathlib
import unicodedata
from dataclasses import dataclass
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "schemas" / "surge-v0.9-input.schema.json"


class PacketRejected(ValueError):
    """A packet failed structural or cross-record admission checks."""


@dataclass(frozen=True)
class Admission:
    status: str
    packet_id: str
    context_id: str
    source_id: str
    source_generation: str
    source_sequence: int
    range_key: str
    range_digest: str
    canonical_request_digest: str


def normalize_source(text: str) -> bytes:
    """Return the immutable NFC UTF-8 snapshot used for all byte anchors."""
    return unicodedata.normalize("NFC", text).encode("utf-8")


def canonical_digest(value: Any) -> str:
    raw = json.dumps(value, ensure_ascii=False, sort_keys=True,
                     separators=(",", ":"), allow_nan=False).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


class RecognitionPacketAdapter:
    """Schema + immutable-source verifier + in-memory idempotency ledger.

    The source snapshot is supplied by the trusted source adapter. Offsets are
    half-open UTF-8 byte offsets into its NFC-normalized bytes. The C++ Core
    remains authoritative for durable sequencing and semantic state commits.
    """

    def __init__(self, schema_path: pathlib.Path | str = SCHEMA_PATH):
        schema = json.loads(pathlib.Path(schema_path).read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        self._validator = Draft202012Validator(schema, format_checker=FormatChecker())
        self._idempotency: dict[str, str] = {}
        self._ranges: dict[str, tuple[str, str, int]] = {}
        self._sequences: dict[tuple[str, str, str, int], str] = {}
        self._packets: dict[str, Admission] = {}

    def admit(self, packet: dict[str, Any], source_snapshot: str) -> Admission:
        errors = sorted(self._validator.iter_errors(packet),
                        key=lambda e: (list(map(str, e.absolute_path)), e.message))
        if errors:
            first = errors[0]
            path = "/".join(map(str, first.absolute_path)) or "$"
            raise PacketRejected(f"SCHEMA_INVALID:{path}:{first.message}")

        pos = packet["source_position"]
        source = normalize_source(source_snapshot)
        start, end = pos["start_offset"], pos["end_offset"]
        if end <= start or end > len(source):
            raise PacketRejected("SOURCE_RANGE_OUT_OF_BOUNDS")
        if packet["content"].get("normalized_digest") != hashlib.sha256(source).hexdigest():
            raise PacketRejected("SOURCE_SNAPSHOT_DIGEST_MISMATCH")

        actual_range_digest = hashlib.sha256(source[start:end]).hexdigest()
        if actual_range_digest.lower() != pos["source_range_digest"].lower():
            raise PacketRejected("SOURCE_RANGE_DIGEST_MISMATCH")

        # A revision is a new immutable record, never an overwrite.
        revision = packet["finalization"] == "REVISION"
        revision_seq = pos.get("source_revision_sequence")
        revision_ref = pos.get("revision_of_ref")
        if revision and (not revision_seq or not revision_ref):
            raise PacketRejected("REVISION_LINEAGE_REQUIRED")
        if not revision and (revision_seq is not None or revision_ref is not None):
            raise PacketRejected("REVISION_FIELDS_REQUIRE_REVISION_PACKET")

        context_id = packet["context_id"]
        source_id = packet["source_id"]
        generation = pos["source_generation"]
        sequence = pos["source_sequence"]
        stream_key = (context_id, source_id, generation, sequence)
        range_key = "|".join((context_id, source_id, generation,
                              pos["block_ref"], pos["offset_basis"],
                              str(start), str(end)))
        request_digest = canonical_digest(packet)
        packet_id = packet["packet_id"]

        previous_request = self._idempotency.get(packet["idempotency_key"])
        if previous_request is not None and previous_request != request_digest:
            raise PacketRejected("IDEMPOTENCY_KEY_REUSED")

        prior_sequence_packet = self._sequences.get(stream_key)
        if prior_sequence_packet is not None and prior_sequence_packet != packet_id:
            raise PacketRejected("SOURCE_SEQUENCE_REUSED")

        prior_range = self._ranges.get(range_key)
        if prior_range is not None:
            prior_digest, prior_packet_id, prior_revision_seq = prior_range
            if not revision:
                if prior_digest != actual_range_digest:
                    raise PacketRejected("RANGE_CONTENT_CONFLICT")
                if previous_request == request_digest and prior_packet_id == packet_id:
                    return self._packets[packet_id]
                raise PacketRejected("RANGE_ALREADY_ADMITTED")
            if revision_ref != prior_packet_id:
                raise PacketRejected("REVISION_TARGET_MISMATCH")
            if revision_seq <= prior_revision_seq:
                raise PacketRejected("SOURCE_REVISION_SEQUENCE_NOT_MONOTONIC")
            if prior_digest == actual_range_digest:
                raise PacketRejected("SOURCE_REVISION_UNCHANGED")
        elif revision:
            raise PacketRejected("REVISION_TARGET_NOT_FOUND")

        if packet_id in self._packets:
            existing = self._packets[packet_id]
            if existing.canonical_request_digest != request_digest:
                raise PacketRejected("PACKET_ID_REUSED")
            return existing

        self._ranges[range_key] = (actual_range_digest, packet_id,
                                   revision_seq if revision else 0)
        admission = Admission(
            status="REVISION_ADMITTED" if revision else "ADMITTED", packet_id=packet_id, context_id=context_id,
            source_id=source_id, source_generation=generation,
            source_sequence=sequence, range_key=range_key,
            range_digest=actual_range_digest,
            canonical_request_digest=request_digest)
        self._idempotency[packet["idempotency_key"]] = request_digest
        self._sequences[stream_key] = packet_id
        self._packets[packet_id] = admission
        return admission
