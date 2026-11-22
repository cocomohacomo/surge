"""JSON ProjectionBatch validation and ctypes bridge to the atomic SURGE C ABI.

This adapter performs transport validation only. It does not classify content,
choose a true interpretation, or change Focus.
"""
from __future__ import annotations

import ctypes
import hashlib
import json
import math
import pathlib
import unicodedata
from dataclasses import dataclass
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker
from surge_adapter._paths import schema_path


SCHEMA_PATH = schema_path("projection-batch-v0.1.schema.json")


class ProjectionRejected(ValueError):
    """A ProjectionBatch failed schema, lineage, or Core admission checks."""


@dataclass(frozen=True)
class ProjectionCommit:
    state_version: int
    duplicate: bool
    packet_status: str


class _Packet(ctypes.Structure):
    _fields_ = [
        ("packet_id", ctypes.c_char_p), ("idempotency_key", ctypes.c_char_p),
        ("context_id", ctypes.c_char_p), ("source_id", ctypes.c_char_p),
        ("source_generation", ctypes.c_char_p), ("block_ref", ctypes.c_char_p),
        ("offset_basis", ctypes.c_char_p), ("start_offset", ctypes.c_uint64),
        ("end_offset", ctypes.c_uint64), ("source_sequence", ctypes.c_uint64),
        ("source_snapshot_digest", ctypes.c_char_p), ("source_range_digest", ctypes.c_char_p),
        ("canonical_request_digest", ctypes.c_char_p), ("revision_of_packet_id", ctypes.c_char_p),
        ("revision", ctypes.c_int), ("revision_sequence", ctypes.c_uint64),
    ]


class _Metadata(ctypes.Structure):
    _fields_ = [
        ("projection_id", ctypes.c_char_p), ("idempotency_key", ctypes.c_char_p),
        ("context_id", ctypes.c_char_p), ("producer_id", ctypes.c_char_p),
        ("producer_version", ctypes.c_char_p), ("policy_version", ctypes.c_char_p),
        ("base_state_version", ctypes.c_uint64),
    ]


class _Node(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
        ("content", ctypes.c_char_p), ("references", ctypes.POINTER(ctypes.c_char_p)),
        ("reference_count", ctypes.c_size_t),
    ]


class _Relation(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
        ("from_id", ctypes.c_char_p), ("to_id", ctypes.c_char_p),
        ("weight", ctypes.c_double),
    ]


class _Membership(ctypes.Structure):
    _fields_ = [
        ("relation_id", ctypes.c_char_p), ("member_ref", ctypes.c_char_p),
        ("cluster_ref", ctypes.c_char_p), ("upstream_weight", ctypes.c_double),
    ]


class _Placement(ctypes.Structure):
    _fields_ = [
        ("cluster_id", ctypes.c_char_p), ("x", ctypes.c_double),
        ("y", ctypes.c_double), ("z", ctypes.c_double),
    ]


class ProjectionBatchAdapter:
    """Validate JSON ProjectionBatch objects and commit through the native Core."""

    def __init__(self, schema_file: str | pathlib.Path = SCHEMA_PATH):
        schema = json.loads(pathlib.Path(schema_file).read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        self._validator = Draft202012Validator(schema, format_checker=FormatChecker())

    def validate(self, batch: dict[str, Any]) -> bytes:
        errors = sorted(self._validator.iter_errors(batch),
                        key=lambda error: (list(map(str, error.absolute_path)), error.message))
        if errors:
            first = errors[0]
            location = "/".join(map(str, first.absolute_path)) or "$"
            raise ProjectionRejected(f"SCHEMA_INVALID:{location}:{first.message}")

        source_text = batch["normalized_source_snapshot"]
        normalized = unicodedata.normalize("NFC", source_text)
        if normalized != source_text:
            raise ProjectionRejected("SOURCE_SNAPSHOT_NOT_NFC")
        source = normalized.encode("utf-8")
        packet = batch["source_packet"]
        start, end = packet["start_offset"], packet["end_offset"]
        if start < 0 or end <= start or end > len(source):
            raise ProjectionRejected("SOURCE_RANGE_OUT_OF_BOUNDS")
        digest = hashlib.sha256(source).hexdigest()
        range_digest = hashlib.sha256(source[start:end]).hexdigest()
        if packet["source_snapshot_digest"] != digest:
            raise ProjectionRejected("SOURCE_SNAPSHOT_DIGEST_MISMATCH")
        if packet["source_range_digest"] != range_digest:
            raise ProjectionRejected("SOURCE_RANGE_DIGEST_MISMATCH")
        if packet["context_id"] != batch["context_id"]:
            raise ProjectionRejected("PROJECTION_CONTEXT_PACKET_MISMATCH")
        if batch["source_event_refs"] != [packet["packet_id"]]:
            raise ProjectionRejected("PROJECTION_SOURCE_PACKET_MISMATCH")

        string_values = [
            batch["projection_id"], batch["idempotency_key"], batch["context_id"],
            batch["producer"]["producer_id"], batch["producer"]["producer_version"],
            batch["policy_version"], packet["packet_id"], packet["idempotency_key"],
            packet["context_id"], packet["source_id"], packet["source_generation"],
            packet["block_ref"], packet["offset_basis"], packet["source_snapshot_digest"],
            packet["source_range_digest"], packet["canonical_request_digest"],
            packet.get("revision_of_packet_id", ""),
            *batch["source_event_refs"], *batch["evidence_refs"],
        ]
        for node in batch["nodes"]:
            string_values.extend([node["id"], node["kind"], node["content"], *node["references"]])
        for relation in batch["relations"]:
            string_values.extend([relation["id"], relation["kind"], relation["from"], relation["to"]])
        for membership in batch["memberships"]:
            string_values.extend([membership["relation_id"], membership["member_ref"], membership["cluster_ref"]])
        for placement in batch["spatial_placements"]:
            string_values.append(placement["cluster_id"])
        if any("\x00" in value for value in string_values):
            raise ProjectionRejected("C_API_STRING_CONTAINS_NUL")

        node_ids = {node["id"] for node in batch["nodes"]}
        if len(node_ids) != len(batch["nodes"]):
            raise ProjectionRejected("EMPTY_OR_DUPLICATE_NODE_ID")
        evidence_nodes = {
            node["id"]: node for node in batch["nodes"] if node["kind"] == "EVIDENCE"
        }
        if not set(batch["evidence_refs"]).issubset(evidence_nodes):
            raise ProjectionRejected("EVIDENCE_NODE_NOT_FOUND")
        for relation in batch["relations"]:
            if not math.isfinite(relation["weight"]):
                raise ProjectionRejected("NON_FINITE_RELATION_WEIGHT")
            if relation["weight"] < 0.0 or relation["weight"] > 1.0:
                raise ProjectionRejected("RELATION_WEIGHT_OUT_OF_RANGE")
        for membership in batch["memberships"]:
            if not math.isfinite(membership["upstream_weight"]):
                raise ProjectionRejected("NON_FINITE_MEMBERSHIP_WEIGHT")
            if membership["upstream_weight"] < 0.0 or membership["upstream_weight"] > 1.0:
                raise ProjectionRejected("MEMBERSHIP_WEIGHT_OUT_OF_RANGE")
        for placement in batch["spatial_placements"]:
            if not all(math.isfinite(placement["position"][axis]) for axis in ("x", "y", "z")):
                raise ProjectionRejected("INVALID_SPATIAL_PLACEMENT")
        return source

    @staticmethod
    def _bind(core: Any) -> None:
        lib = core._lib
        charp = ctypes.POINTER(ctypes.c_char)
        lib.surge_projection_batch_admit_and_apply.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(_Packet),
            ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t,
            ctypes.POINTER(_Metadata),
            ctypes.POINTER(ctypes.c_char_p), ctypes.c_size_t,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.POINTER(_Membership), ctypes.c_size_t,
            ctypes.POINTER(_Placement), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            charp, ctypes.c_size_t, charp, ctypes.c_size_t,
        ]
        lib.surge_projection_batch_admit_and_apply.restype = ctypes.c_int

    def commit(self, batch: dict[str, Any], core: Any) -> ProjectionCommit:
        source = self.validate(batch)
        self._bind(core)
        packet_data = batch["source_packet"]
        producer = batch["producer"]
        packet = _Packet(
            packet_data["packet_id"].encode(), packet_data["idempotency_key"].encode(),
            packet_data["context_id"].encode(), packet_data["source_id"].encode(),
            packet_data["source_generation"].encode(), packet_data["block_ref"].encode(),
            packet_data["offset_basis"].encode(), packet_data["start_offset"],
            packet_data["end_offset"], packet_data["source_sequence"],
            packet_data["source_snapshot_digest"].encode(),
            packet_data["source_range_digest"].encode(),
            packet_data["canonical_request_digest"].encode(),
            packet_data.get("revision_of_packet_id", "").encode(),
            int(packet_data.get("revision", False)), packet_data.get("revision_sequence", 0),
        )
        metadata = _Metadata(
            batch["projection_id"].encode(), batch["idempotency_key"].encode(),
            batch["context_id"].encode(), producer["producer_id"].encode(),
            producer["producer_version"].encode(), batch["policy_version"].encode(),
            batch["base_state_version"],
        )
        evidence_values = [value.encode() for value in batch["evidence_refs"]]
        evidence_array = (ctypes.c_char_p * len(evidence_values))(*evidence_values)

        node_ref_arrays = []
        node_values = []
        for item in batch["nodes"]:
            refs = [value.encode() for value in item["references"]]
            ref_array = (ctypes.c_char_p * len(refs))(*refs) if refs else None
            node_ref_arrays.append(ref_array)
            node_values.append(_Node(
                item["id"].encode(), item["kind"].encode(), item["content"].encode(),
                ref_array, len(refs),
            ))
        node_array = (_Node * len(node_values))(*node_values)

        relation_values = [
            _Relation(item["id"].encode(), item["kind"].encode(), item["from"].encode(),
                      item["to"].encode(), item["weight"])
            for item in batch["relations"]
        ]
        relation_array = None if not relation_values else (_Relation * len(relation_values))(*relation_values)
        membership_values = [
            _Membership(item["relation_id"].encode(), item["member_ref"].encode(),
                        item["cluster_ref"].encode(), item["upstream_weight"])
            for item in batch["memberships"]
        ]
        membership_array = None if not membership_values else (_Membership * len(membership_values))(*membership_values)
        placement_values = [
            _Placement(item["cluster_id"].encode(), item["position"]["x"],
                       item["position"]["y"], item["position"]["z"])
            for item in batch["spatial_placements"]
        ]
        placement_array = None if not placement_values else (_Placement * len(placement_values))(*placement_values)

        snapshot_array = (ctypes.c_ubyte * len(source)).from_buffer_copy(source)
        duplicate = ctypes.c_int(0)
        state_version = ctypes.c_uint64(0)
        status = ctypes.create_string_buffer(128)
        error = ctypes.create_string_buffer(1024)
        result = core._lib.surge_projection_batch_admit_and_apply(
            core._handle, ctypes.byref(packet), snapshot_array, len(source),
            ctypes.byref(metadata), evidence_array, len(evidence_values),
            node_array, len(node_values), relation_array, len(relation_values),
            membership_array, len(membership_values), placement_array, len(placement_values),
            ctypes.byref(duplicate), ctypes.byref(state_version),
            status, len(status), error, len(error),
        )
        if not result:
            raise ProjectionRejected(error.value.decode("utf-8", errors="replace") or
                                     "PROJECTION_COMMIT_FAILED")
        return ProjectionCommit(
            state_version=int(state_version.value),
            duplicate=bool(duplicate.value),
            packet_status=status.value.decode("utf-8", errors="replace"),
        )
