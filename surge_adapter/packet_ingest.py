"""RecognitionPacket admission boundary.

This module validates packet shape and source-range integrity before a packet
is handed to the C++ Core. It deliberately does not perform semantic inference
or authorize a state commit.
"""
from __future__ import annotations

import ctypes
import hashlib
import os
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

    def admit_to_core(self, packet: dict[str, Any], source_snapshot: str,
                      core: "CppCoreBridge") -> Admission:
        """Validate at the Python boundary, then commit through the C++ Core.

        Unlike admit(), this method does not mutate the adapter's process-local
        ledger. The journal-backed C++ coordinator is the sole admission authority.
        """
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
        snapshot_digest = hashlib.sha256(source).hexdigest()
        if packet["content"].get("normalized_digest") != snapshot_digest:
            raise PacketRejected("SOURCE_SNAPSHOT_DIGEST_MISMATCH")
        range_digest = hashlib.sha256(source[start:end]).hexdigest()
        if range_digest.lower() != pos["source_range_digest"].lower():
            raise PacketRejected("SOURCE_RANGE_DIGEST_MISMATCH")
        revision = packet["finalization"] == "REVISION"
        revision_seq = pos.get("source_revision_sequence", 0)
        revision_ref = pos.get("revision_of_ref", "")
        if revision and (not revision_seq or not revision_ref):
            raise PacketRejected("REVISION_LINEAGE_REQUIRED")
        if not revision and (revision_seq or revision_ref):
            raise PacketRejected("REVISION_FIELDS_REQUIRE_REVISION_PACKET")
        return core.admit(packet, source, snapshot_digest, range_digest,
                          canonical_digest(packet), revision, revision_seq,
                          revision_ref)


    def admit_and_apply_to_core(self, packet: dict[str, Any], source_snapshot: str,
                                core: "CppCoreBridge", delta: dict[str, Any]):
        """Atomically admit a RecognitionPacket and commit its SemanticDelta."""
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
        snapshot_digest = hashlib.sha256(source).hexdigest()
        if packet["content"].get("normalized_digest") != snapshot_digest:
            raise PacketRejected("SOURCE_SNAPSHOT_DIGEST_MISMATCH")
        range_digest = hashlib.sha256(source[start:end]).hexdigest()
        if range_digest.lower() != pos["source_range_digest"].lower():
            raise PacketRejected("SOURCE_RANGE_DIGEST_MISMATCH")
        revision = packet["finalization"] == "REVISION"
        revision_sequence = pos.get("source_revision_sequence", 0)
        revision_ref = pos.get("revision_of_ref", "")
        if revision and (not revision_sequence or not revision_ref):
            raise PacketRejected("REVISION_LINEAGE_REQUIRED")
        if not revision and (revision_sequence or revision_ref):
            raise PacketRejected("REVISION_FIELDS_REQUIRE_REVISION_PACKET")
        return core.admit_and_apply(packet, source, snapshot_digest, range_digest,
            canonical_digest(packet), revision, revision_sequence, revision_ref, delta)

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

        # Reject packet-ID reuse before touching any ledger index.
        if packet_id in self._packets:
            existing = self._packets[packet_id]
            if existing.canonical_request_digest != request_digest:
                raise PacketRejected("PACKET_ID_REUSED")
            return existing

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


class CppCoreBridge:
    """ctypes bridge to the journal-backed C++ PacketAdmissionCoordinator."""

    def __init__(self, journal_path: str | pathlib.Path,
                 library_path: str | pathlib.Path | None = None,
                 resource_limits: dict[str, int] | None = None):
        import platform
        if library_path is None:
            configured = os.environ.get("SURGE_CORE_LIBRARY")
            candidates = ([pathlib.Path(configured)] if configured else []) + [
                ROOT / "build" / "libsurge_core.so",
                ROOT / "build" / "libsurge_core.dylib",
                ROOT / "build" / "surge_core.dll",
                ROOT / "build" / "Release" / "surge_core.dll",
            ]
            library = next((p for p in candidates if p.is_file()), None)
            if library is None:
                raise FileNotFoundError(
                    "SURGE C++ shared library not found; build it with CMake "
                    "or set SURGE_CORE_LIBRARY")
        else:
            library = pathlib.Path(library_path)
        self._lib = ctypes.CDLL(str(library))
        charp = ctypes.POINTER(ctypes.c_char)
        self._lib.surge_packet_core_open.argtypes = [
            ctypes.c_char_p, charp, ctypes.c_size_t]
        self._lib.surge_packet_core_open.restype = ctypes.c_void_p
        class _ResourceBudgetLimits(ctypes.Structure):
            _fields_ = [(name, ctypes.c_size_t) for name in (
                "resident_bytes", "transient_bytes", "retained_events",
                "retained_clusters", "decision_candidates", "decision_evidence")]
        self._ResourceBudgetLimits = _ResourceBudgetLimits
        self._lib.surge_packet_core_open_with_limits.argtypes = [
            ctypes.c_char_p, ctypes.POINTER(_ResourceBudgetLimits), charp, ctypes.c_size_t]
        self._lib.surge_packet_core_open_with_limits.restype = ctypes.c_void_p
        self._lib.surge_packet_core_close.argtypes = [ctypes.c_void_p]
        self._lib.surge_packet_core_close.restype = None
        self._lib.surge_packet_core_admit.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_int, ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_uint64), charp, ctypes.c_size_t,
            charp, ctypes.c_size_t]
        self._lib.surge_packet_core_admit.restype = ctypes.c_int
        self._lib.surge_packet_core_state_version.argtypes = [ctypes.c_void_p]
        self._lib.surge_packet_core_state_version.restype = ctypes.c_uint64
        self._lib.surge_packet_core_packet_count.argtypes = [ctypes.c_void_p]
        self._lib.surge_packet_core_packet_count.restype = ctypes.c_size_t
        error = ctypes.create_string_buffer(1024)
        encoded_path = str(pathlib.Path(journal_path)).encode("utf-8")
        if resource_limits is None:
            self._handle = self._lib.surge_packet_core_open(encoded_path, error, len(error))
        else:
            required = {"resident_bytes", "transient_bytes", "retained_events",
                        "retained_clusters", "decision_candidates", "decision_evidence"}
            if set(resource_limits) != required:
                raise ValueError("resource_limits must define exactly: " + ", ".join(sorted(required)))
            values = []
            for key in ("resident_bytes", "transient_bytes", "retained_events",
                        "retained_clusters", "decision_candidates", "decision_evidence"):
                value = resource_limits[key]
                if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                    raise ValueError(f"resource_limits[{key!r}] must be a non-negative integer")
                if value > (1 << (ctypes.sizeof(ctypes.c_size_t) * 8)) - 1:
                    raise ValueError(f"resource_limits[{key!r}] exceeds size_t")
                values.append(value)
            limits = self._ResourceBudgetLimits(*values)
            self._handle = self._lib.surge_packet_core_open_with_limits(
                encoded_path, ctypes.byref(limits), error, len(error))
        if not self._handle:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CORE_OPEN_FAILED")

    def close(self) -> None:
        if getattr(self, "_handle", None):
            self._lib.surge_packet_core_close(self._handle)
            self._handle = None

    def __enter__(self) -> "CppCoreBridge":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    @property
    def state_version(self) -> int:
        return int(self._lib.surge_packet_core_state_version(self._handle))

    @property
    def packet_count(self) -> int:
        return int(self._lib.surge_packet_core_packet_count(self._handle))

    def admit(self, packet: dict[str, Any], source: bytes,
              snapshot_digest: str, range_digest: str,
              request_digest: str, revision: bool = False,
              revision_sequence: int = 0,
              revision_of_ref: str = "") -> Admission:
        pos = packet["source_position"]
        strings = [
            packet["packet_id"], packet["idempotency_key"], packet["context_id"],
            packet["source_id"], pos["source_generation"], pos["block_ref"],
            pos["offset_basis"], snapshot_digest, range_digest, request_digest,
            revision_of_ref]
        if any("\\x00" in value for value in strings):
            raise PacketRejected("C_API_STRING_CONTAINS_NUL")
        encoded = [value.encode("utf-8") for value in strings]
        snapshot_buffer = (ctypes.c_ubyte * len(source)).from_buffer_copy(source)
        duplicate = ctypes.c_int(0)
        version = ctypes.c_uint64(0)
        watermark = ctypes.c_uint64(0)
        status = ctypes.create_string_buffer(128)
        error = ctypes.create_string_buffer(1024)
        result = self._lib.surge_packet_core_admit(
            self._handle, *encoded[:7],
            int(pos["start_offset"]), int(pos["end_offset"]),
            int(pos["source_sequence"]), encoded[7], encoded[8], encoded[9],
            encoded[10], int(revision), int(revision_sequence),
            snapshot_buffer, len(source), ctypes.byref(duplicate),
            ctypes.byref(version), ctypes.byref(watermark),
            status, len(status), error, len(error))
        if not result:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CORE_ADMISSION_FAILED")
        range_key = "|".join((packet["context_id"], packet["source_id"],
                              pos["source_generation"], pos["block_ref"],
                              pos["offset_basis"], str(pos["start_offset"]),
                              str(pos["end_offset"])))
        return Admission(
            status=status.value.decode("utf-8") or "DUPLICATE",
            packet_id=packet["packet_id"], context_id=packet["context_id"],
            source_id=packet["source_id"],
            source_generation=pos["source_generation"],
            source_sequence=int(pos["source_sequence"]),
            range_key=range_key, range_digest=range_digest,
            canonical_request_digest=request_digest)
