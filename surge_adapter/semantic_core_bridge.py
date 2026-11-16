"""ctypes access to the journal-backed C++ ContextGraph SemanticDelta API."""
from __future__ import annotations
import ctypes
import json
from typing import Any
from surge_adapter.packet_ingest import Admission, CppCoreBridge, PacketRejected, normalize_source

class _Packet(ctypes.Structure):
    _fields_ = [("packet_id", ctypes.c_char_p), ("idempotency_key", ctypes.c_char_p),
                ("context_id", ctypes.c_char_p), ("source_id", ctypes.c_char_p),
                ("source_generation", ctypes.c_char_p), ("block_ref", ctypes.c_char_p),
                ("offset_basis", ctypes.c_char_p), ("start_offset", ctypes.c_uint64),
                ("end_offset", ctypes.c_uint64), ("source_sequence", ctypes.c_uint64),
                ("source_snapshot_digest", ctypes.c_char_p), ("source_range_digest", ctypes.c_char_p),
                ("canonical_request_digest", ctypes.c_char_p), ("revision_of_packet_id", ctypes.c_char_p),
                ("revision", ctypes.c_int), ("revision_sequence", ctypes.c_uint64)]

class _Node(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("content", ctypes.c_char_p),
                ("references", ctypes.POINTER(ctypes.c_char_p)),
                ("reference_count", ctypes.c_size_t)]

class _Relation(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("from_id", ctypes.c_char_p), ("to_id", ctypes.c_char_p)]

class _Anchor(ctypes.Structure):
    _fields_ = [("anchor_id", ctypes.c_char_p), ("source_digest", ctypes.c_char_p),
                ("offset_basis", ctypes.c_char_p), ("start_offset", ctypes.c_uint64),
                ("end_offset", ctypes.c_uint64), ("exact_text", ctypes.c_char_p),
                ("packet_ref", ctypes.c_char_p)]

class _Scope(ctypes.Structure):
    _fields_ = [("span_id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("source_digest", ctypes.c_char_p), ("offset_basis", ctypes.c_char_p),
                ("opener", _Anchor), ("has_closer", ctypes.c_int), ("closer", _Anchor),
                ("target_refs", ctypes.POINTER(ctypes.c_char_p)), ("target_count", ctypes.c_size_t),
                ("antecedent_refs", ctypes.POINTER(ctypes.c_char_p)), ("antecedent_count", ctypes.c_size_t),
                ("attribution_ref", ctypes.c_char_p), ("modality_kind", ctypes.c_char_p),
                ("temporal_role", ctypes.c_char_p), ("parent_ref", ctypes.c_char_p),
                ("packet_refs", ctypes.POINTER(ctypes.c_char_p)), ("packet_count", ctypes.c_size_t)]

class SemanticCoreBridge(CppCoreBridge):
    """Packet admission + typed SemanticDelta commit over one shared journal."""

    def __init__(self, journal_path, library_path=None):
        super().__init__(journal_path, library_path)
        lib = self._lib
        lib.surge_packet_core_admit_and_apply.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(_Packet), ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.POINTER(_Scope), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_packet_core_admit_and_apply.restype = ctypes.c_int
        lib.surge_context_graph_apply_scoped.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.POINTER(_Scope), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply_scoped.restype = ctypes.c_int
        lib.surge_context_graph_scope_count.argtypes = [ctypes.c_void_p]
        lib.surge_context_graph_scope_count.restype = ctypes.c_size_t
        lib.surge_question_aspect_set_status.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_aspect_set_status.restype = ctypes.c_int
        lib.surge_question_aspect_get_status.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_aspect_get_status.restype = ctypes.c_int
        lib.surge_question_get_aggregate_status.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_get_aggregate_status.restype = ctypes.c_int
        lib.surge_question_get_reevaluation_candidates.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_get_reevaluation_candidates.restype = ctypes.c_int
        lib.surge_question_route_contradiction.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_route_contradiction.restype = ctypes.c_int
        lib.surge_question_route_contradiction_scoped.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_route_contradiction_scoped.restype = ctypes.c_int
        lib.surge_context_graph_dependency_evaluation.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_dependency_evaluation.restype = ctypes.c_int
        lib.surge_context_graph_apply.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply.restype = ctypes.c_int
        lib.surge_context_graph_apply_claim_validity.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply_claim_validity.restype = ctypes.c_int
        lib.surge_context_graph_apply_claim_validity_reviewed.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply_claim_validity_reviewed.restype = ctypes.c_int
        lib.surge_context_graph_claim_validity_at.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_claim_validity_at.restype = ctypes.c_int

        lib.surge_temporal_boundary_add.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_boundary_add.restype = ctypes.c_int
        lib.surge_temporal_boundary_resolve_candidate.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_boundary_resolve_candidate.restype = ctypes.c_int
        lib.surge_temporal_boundary_get.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_boundary_get.restype = ctypes.c_int
        lib.surge_temporal_boundary_clear.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_boundary_clear.restype = ctypes.c_int
        lib.surge_temporal_boundary_pending_count.argtypes = [ctypes.c_void_p]
        lib.surge_temporal_boundary_pending_count.restype = ctypes.c_size_t
        lib.surge_temporal_question_get.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_question_get.restype = ctypes.c_int
        lib.surge_temporal_question_count.argtypes = [ctypes.c_void_p]
        lib.surge_temporal_question_count.restype = ctypes.c_size_t
        lib.surge_context_graph_focus_get.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_focus_get.restype = ctypes.c_int


    @property
    def scope_count(self) -> int:
        return int(self._lib.surge_context_graph_scope_count(self._handle))


    def admit_and_apply(self, packet, source, snapshot_digest, range_digest,
                        request_digest, revision, revision_sequence, revision_ref,
                        delta):
        pos = packet["source_position"]
        packet_input = _Packet(
            packet["packet_id"].encode(), packet["idempotency_key"].encode(),
            packet["context_id"].encode(), packet["source_id"].encode(),
            pos["source_generation"].encode(), pos["block_ref"].encode(),
            pos["offset_basis"].encode(), int(pos["start_offset"]),
            int(pos["end_offset"]), int(pos["source_sequence"]),
            snapshot_digest.encode(), range_digest.encode(), request_digest.encode(),
            (revision_ref or "").encode(), 1 if revision else 0, int(revision_sequence))
        keepalive = []
        nodes = []
        for item in delta["nodes"]:
            refs = [str(v).encode() for v in item.get("references", [])]
            ref_array = (ctypes.c_char_p * len(refs))(*refs) if refs else None
            if ref_array is not None:
                keepalive.append(ref_array)
            nodes.append(_Node(str(item["id"]).encode(), str(item["kind"]).encode(),
                str(item["content"]).encode(),
                ctypes.cast(ref_array, ctypes.POINTER(ctypes.c_char_p)) if ref_array is not None else None,
                len(refs)))
        node_array = (_Node * len(nodes))(*nodes) if nodes else None
        relations = [_Relation(str(r["id"]).encode(), str(r["kind"]).encode(),
                               str(r["from"]).encode(), str(r["to"]).encode())
                     for r in delta["relations"]]
        relation_array = (_Relation * len(relations))(*relations) if relations else None
        scopes = []
        for item in delta.get("scope_spans", []):
            def anchor(value):
                return _Anchor(str(value["anchor_id"]).encode(),
                               str(value["source_digest"]).encode(),
                               str(value["offset_basis"]).encode(),
                               int(value["start_offset"]), int(value["end_offset"]),
                               str(value["exact_text"]).encode(),
                               str(value["packet_ref"]).encode())
            opener = anchor(item["opener"])
            closer_value = item.get("closer")
            closer = anchor(closer_value) if closer_value else _Anchor()
            target = [str(v).encode() for v in item.get("target_refs", [])]
            antecedents = [str(v).encode() for v in item.get("antecedent_refs", [])]
            packets = [str(v).encode() for v in item.get("packet_refs", [])]
            target_array = (ctypes.c_char_p * len(target))(*target) if target else None
            antecedent_array = (ctypes.c_char_p * len(antecedents))(*antecedents) if antecedents else None
            packet_array = (ctypes.c_char_p * len(packets))(*packets) if packets else None
            keepalive.extend([x for x in (target_array, antecedent_array, packet_array) if x is not None])
            scopes.append(_Scope(str(item["span_id"]).encode(), str(item["kind"]).encode(),
                str(item["source_digest"]).encode(), str(item["offset_basis"]).encode(),
                opener, 1 if closer_value else 0, closer,
                ctypes.cast(target_array, ctypes.POINTER(ctypes.c_char_p)) if target_array is not None else None,
                len(target), ctypes.cast(antecedent_array, ctypes.POINTER(ctypes.c_char_p)) if antecedent_array is not None else None,
                len(antecedents), str(item.get("attribution_ref", "")).encode(),
                str(item.get("modality_kind", "")).encode(), str(item.get("temporal_role", "")).encode(),
                str(item.get("parent_ref", "")).encode(),
                ctypes.cast(packet_array, ctypes.POINTER(ctypes.c_char_p)) if packet_array is not None else None,
                len(packets)))
        scope_array = (_Scope * len(scopes))(*scopes) if scopes else None
        snapshot_buffer = (ctypes.c_ubyte * len(source)).from_buffer_copy(source)
        focus = delta.get("focus")
        focus_cluster = str(focus["cluster_id"]).encode() if focus else None
        focus_interest = str(focus.get("interest_id", "")).encode() if focus else None
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        status, error = ctypes.create_string_buffer(128), ctypes.create_string_buffer(1024)
        ok = self._lib.surge_packet_core_admit_and_apply(
            self._handle, ctypes.byref(packet_input), snapshot_buffer, len(source),
            str(delta["delta_id"]).encode(), str(delta["idempotency_key"]).encode(),
            int(delta["expected_state_version"]), node_array, len(nodes),
            relation_array, len(relations), scope_array, len(scopes),
            focus_cluster, focus_interest, ctypes.byref(duplicate),
            ctypes.byref(version), status, len(status), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "ATOMIC_SEMANTIC_COMMIT_FAILED")
        range_key = "|".join((packet["context_id"], packet["source_id"],
            pos["source_generation"], pos["block_ref"], pos["offset_basis"],
            str(pos["start_offset"]), str(pos["end_offset"])))
        admission = Admission(status=status.value.decode("utf-8") or "DUPLICATE",
            packet_id=packet["packet_id"], context_id=packet["context_id"],
            source_id=packet["source_id"], source_generation=pos["source_generation"],
            source_sequence=int(pos["source_sequence"]), range_key=range_key,
            range_digest=range_digest, canonical_request_digest=request_digest)
        return admission, {"committed": True, "duplicate": bool(duplicate.value),
                           "state_version": int(version.value)}


    def dependency_evaluation(self, delta_id: str) -> dict[str, Any]:
        """Return the durable dependency evaluation for one committed SemanticDelta."""
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(256)
        fn = self._lib.surge_context_graph_dependency_evaluation
        ok = fn(self._handle, str(delta_id).encode("utf-8"), None, 0,
                ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != \
                "DEPENDENCY_EVALUATION_BUFFER_TOO_SMALL":
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "DEPENDENCY_EVALUATION_READ_FAILED")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(256)
        ok = fn(self._handle, str(delta_id).encode("utf-8"), output, len(output),
                ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "DEPENDENCY_EVALUATION_READ_FAILED")
        try:
            value = json.loads(output.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("DEPENDENCY_EVALUATION_JSON_INVALID") from exc
        if not isinstance(value, dict) or value.get("delta_id") != delta_id:
            raise PacketRejected("DEPENDENCY_EVALUATION_RESPONSE_INVALID")
        return value

    def apply_claim_validity(self, *, delta_id: str, idempotency_key: str,
                             expected_state_version: int, claim_id: str,
                             valid_from: str, valid_until: str | None,
                             basis_evidence_id: str, revision: int) -> dict[str, Any]:
        """Append a valid-time interval revision without overwriting prior history."""
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_context_graph_apply_claim_validity(
            self._handle, delta_id.encode("utf-8"), idempotency_key.encode("utf-8"),
            int(expected_state_version), claim_id.encode("utf-8"),
            valid_from.encode("utf-8"), (valid_until or "").encode("utf-8"),
            basis_evidence_id.encode("utf-8"), int(revision),
            ctypes.byref(duplicate), ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CLAIM_VALIDITY_REJECTED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value)}

    def apply_approved_temporal_binding(self, binding: dict[str, Any], source_text: str, *,
                                        approved: bool, reviewer_id: str,
                                        review_evidence_id: str, delta_id: str,
                                        idempotency_key: str, expected_state_version: int,
                                        revision: int) -> dict[str, Any]:
        """Commit a reviewed temporal binding with durable review provenance."""
        import unicodedata
        if approved is not True:
            raise PacketRejected("CLAIM_VALIDITY_REVIEW_NOT_APPROVED")
        if not isinstance(binding, dict) or binding.get("binding_status") not in ("BOUND_PENDING_CORE_REVIEW", "CORRECTION_REVIEW_REQUIRED"):
            raise PacketRejected("TEMPORAL_BINDING_NOT_ELIGIBLE")
        if not isinstance(reviewer_id, str) or not reviewer_id.strip() or not isinstance(review_evidence_id, str) or not review_evidence_id.strip():
            raise PacketRejected("CLAIM_VALIDITY_REVIEW_PROVENANCE_REQUIRED")
        span = binding.get("source_span") or {}
        normalized = unicodedata.normalize("NFC", source_text).encode("utf-8")
        start, end = span.get("start_offset"), span.get("end_offset")
        if (span.get("offset_basis") != "UTF8_BYTE_NFC" or
                not isinstance(start, int) or isinstance(start, bool) or
                not isinstance(end, int) or isinstance(end, bool) or
                start < 0 or end <= start or end > len(normalized)):
            raise PacketRejected("CLAIM_VALIDITY_SOURCE_SPAN_INVALID")
        try:
            exact = normalized[start:end].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise PacketRejected("CLAIM_VALIDITY_SOURCE_SPAN_INVALID") from exc
        if exact != span.get("exact_text"):
            raise PacketRejected("CLAIM_VALIDITY_SOURCE_SPAN_MISMATCH")
        candidate = binding.get("temporal_candidate") or {}
        if (candidate.get("status") != "RESOLVED" or
                candidate.get("role") not in ("VALID_FROM", "INTERVAL") or
                not isinstance(candidate.get("valid_from"), str) or not candidate.get("valid_from")):
            raise PacketRejected("CLAIM_VALIDITY_CANDIDATE_NOT_COMMITTABLE")
        claim_id, evidence_id = binding.get("claim_ref"), binding.get("evidence_ref")
        if not isinstance(claim_id, str) or not claim_id or not isinstance(evidence_id, str) or not evidence_id:
            raise PacketRejected("CLAIM_VALIDITY_BINDING_REFERENCES_REQUIRED")
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_context_graph_apply_claim_validity_reviewed(
            self._handle, delta_id.encode("utf-8"), idempotency_key.encode("utf-8"),
            int(expected_state_version), claim_id.encode("utf-8"),
            candidate["valid_from"].encode("utf-8"),
            (candidate.get("valid_until") or "").encode("utf-8"),
            evidence_id.encode("utf-8"), int(revision),
            review_evidence_id.encode("utf-8"), reviewer_id.encode("utf-8"),
            ctypes.byref(duplicate), ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CLAIM_VALIDITY_REVIEWED_COMMIT_FAILED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value), "claim_id": claim_id,
                "revision": int(revision), "review_evidence_id": review_evidence_id,
                "reviewer_id": reviewer_id}

    def claim_validity_at(self, claim_id: str, instant: str) -> str:
        """Evaluate the latest valid-time revision at an explicit RFC3339 instant."""
        output = ctypes.create_string_buffer(64)
        error = ctypes.create_string_buffer(256)
        ok = self._lib.surge_context_graph_claim_validity_at(
            self._handle, claim_id.encode("utf-8"), instant.encode("utf-8"),
            output, len(output), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CLAIM_VALIDITY_QUERY_FAILED")
        return output.value.decode("ascii")

    def apply_semantic_delta(self, delta: dict[str, Any]) -> dict[str, Any]:
        required = {"delta_id", "idempotency_key", "expected_state_version",
                    "nodes", "relations"}
        if not isinstance(delta, dict) or not required.issubset(delta):
            raise PacketRejected("SEMANTIC_DELTA_SHAPE_INVALID")
        # Core checks idempotency before version conflicts, allowing exact retries.
        keepalive: list[Any] = []
        nodes = []
        for item in delta["nodes"]:
            refs = [str(v).encode("utf-8") for v in item.get("references", [])]
            ref_array = (ctypes.c_char_p * len(refs))(*refs) if refs else None
            if ref_array is not None:
                keepalive.append(ref_array)
            nodes.append(_Node(str(item["id"]).encode(), str(item["kind"]).encode(),
                               str(item["content"]).encode(),
                               ctypes.cast(ref_array, ctypes.POINTER(ctypes.c_char_p)) if ref_array is not None else None,
                               len(refs)))
        node_array = (_Node * len(nodes))(*nodes) if nodes else None
        relations = [_Relation(str(r["id"]).encode(), str(r["kind"]).encode(),
                               str(r["from"]).encode(), str(r["to"]).encode())
                     for r in delta["relations"]]
        relation_array = (_Relation * len(relations))(*relations) if relations else None
        scopes = []
        for item in delta.get("scope_spans", []):
            def anchor(value):
                return _Anchor(str(value["anchor_id"]).encode(),
                               str(value["source_digest"]).encode(),
                               str(value["offset_basis"]).encode(),
                               int(value["start_offset"]), int(value["end_offset"]),
                               str(value["exact_text"]).encode(),
                               str(value["packet_ref"]).encode())
            opener = anchor(item["opener"])
            closer_value = item.get("closer")
            closer = anchor(closer_value) if closer_value else _Anchor()
            target = [str(v).encode() for v in item.get("target_refs", [])]
            antecedents = [str(v).encode() for v in item.get("antecedent_refs", [])]
            packets = [str(v).encode() for v in item.get("packet_refs", [])]
            target_array = (ctypes.c_char_p * len(target))(*target) if target else None
            antecedent_array = (ctypes.c_char_p * len(antecedents))(*antecedents) if antecedents else None
            packet_array = (ctypes.c_char_p * len(packets))(*packets) if packets else None
            keepalive.extend([x for x in (target_array, antecedent_array, packet_array) if x is not None])
            scopes.append(_Scope(str(item["span_id"]).encode(), str(item["kind"]).encode(),
                str(item["source_digest"]).encode(), str(item["offset_basis"]).encode(),
                opener, 1 if closer_value else 0, closer,
                ctypes.cast(target_array, ctypes.POINTER(ctypes.c_char_p)) if target_array is not None else None,
                len(target), ctypes.cast(antecedent_array, ctypes.POINTER(ctypes.c_char_p)) if antecedent_array is not None else None,
                len(antecedents), str(item.get("attribution_ref", "")).encode(),
                str(item.get("modality_kind", "")).encode(), str(item.get("temporal_role", "")).encode(),
                str(item.get("parent_ref", "")).encode(),
                ctypes.cast(packet_array, ctypes.POINTER(ctypes.c_char_p)) if packet_array is not None else None,
                len(packets)))
        scope_array = (_Scope * len(scopes))(*scopes) if scopes else None
        source_snapshot = normalize_source(str(delta.get("source_snapshot", "")))
        focus = delta.get("focus")
        focus_cluster = str(focus["cluster_id"]).encode() if focus else None
        focus_interest = str(focus.get("interest_id", "")).encode() if focus else None
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(1024)
        ok = self._lib.surge_context_graph_apply_scoped(
            self._handle, str(delta["delta_id"]).encode(),
            str(delta["idempotency_key"]).encode(),
            int(delta["expected_state_version"]),
            node_array, len(nodes), relation_array, len(relations),
            scope_array, len(scopes), source_snapshot, len(source_snapshot),
            focus_cluster, focus_interest, ctypes.byref(duplicate),
            ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "SEMANTIC_DELTA_REJECTED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value)}


    @staticmethod
    def _decode_temporal_ledger_json(payload: bytes) -> dict[str, Any]:
        try:
            value = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("TEMPORAL_LEDGER_RESPONSE_INVALID") from exc
        if not isinstance(value, dict):
            raise PacketRejected("TEMPORAL_LEDGER_RESPONSE_INVALID")
        return value

    def _temporal_ledger_call(self, function, args: tuple[Any, ...],
                              buffer_error: str = "TEMPORAL_LEDGER_BUFFER_TOO_SMALL") -> dict[str, Any]:
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(512)
        ok = function(*args, None, 0, ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != buffer_error:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "TEMPORAL_LEDGER_READ_FAILED")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(512)
        function(*args, output, len(output), ctypes.byref(required), error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if detail:
            raise PacketRejected(detail)
        return self._decode_temporal_ledger_json(output.value)

    def add_temporal_boundary(self, *, transaction_id: str, idempotency_key: str,
                              event_id: str, expected_state_version: int,
                              claim_id: str, role: str, valid_from: str = "",
                              valid_until: str = "", evidence_id: str,
                              packet_id: str) -> dict[str, Any]:
        if any(not isinstance(v, str) or not v.strip() for v in
               (transaction_id, idempotency_key, event_id, claim_id, role, evidence_id, packet_id)):
            raise PacketRejected("TEMPORAL_LEDGER_ARGUMENT_INVALID")
        args = (self._handle, transaction_id.encode(), idempotency_key.encode(),
                event_id.encode(), int(expected_state_version), claim_id.encode(),
                role.encode(), valid_from.encode(), valid_until.encode(),
                evidence_id.encode(), packet_id.encode())
        result = self._temporal_ledger_call(self._lib.surge_temporal_boundary_add, args)
        if not result.get("committed"):
            raise PacketRejected(result.get("status") or result.get("error") or
                                 "TEMPORAL_LEDGER_ADD_REJECTED")
        return result

    def get_temporal_boundary(self, claim_id: str) -> dict[str, Any] | None:
        if not isinstance(claim_id, str) or not claim_id.strip():
            raise PacketRejected("CLAIM_ID_REQUIRED")
        args = (self._handle, claim_id.encode())
        result = self._temporal_ledger_call(self._lib.surge_temporal_boundary_get, args)
        return result.get("pending") if result.get("found") else None

    def resolve_temporal_boundary_candidate(self, *, transaction_id: str, idempotency_key: str,
            event_id: str, expected_state_version: int, claim_id: str, role: str,
            candidate_id: str) -> dict[str, Any]:
        if any(not isinstance(v, str) or not v.strip() for v in
               (transaction_id, idempotency_key, event_id, claim_id, role, candidate_id)):
            raise PacketRejected("TEMPORAL_LEDGER_ARGUMENT_INVALID")
        args = (self._handle, transaction_id.encode(), idempotency_key.encode(),
                event_id.encode(), int(expected_state_version), claim_id.encode(),
                role.encode(), candidate_id.encode())
        result = self._temporal_ledger_call(self._lib.surge_temporal_boundary_resolve_candidate, args)
        if not result.get("committed"):
            raise PacketRejected(result.get("status") or result.get("error") or "TEMPORAL_LEDGER_RESOLUTION_REJECTED")
        return result

    def clear_temporal_boundary(self, *, transaction_id: str, idempotency_key: str,
                                event_id: str, expected_state_version: int,
                                claim_id: str) -> dict[str, Any]:
        if any(not isinstance(v, str) or not v.strip() for v in
               (transaction_id, idempotency_key, event_id, claim_id)):
            raise PacketRejected("TEMPORAL_LEDGER_ARGUMENT_INVALID")
        args = (self._handle, transaction_id.encode(), idempotency_key.encode(),
                event_id.encode(), int(expected_state_version), claim_id.encode())
        result = self._temporal_ledger_call(self._lib.surge_temporal_boundary_clear, args)
        if not result.get("committed"):
            raise PacketRejected(result.get("status") or result.get("error") or
                                 "TEMPORAL_LEDGER_CLEAR_REJECTED")
        return result

    @property
    def temporal_boundary_pending_count(self) -> int:
        return int(self._lib.surge_temporal_boundary_pending_count(self._handle))

    def get_temporal_questions(self, claim_id: str) -> list[dict[str, Any]]:
        if not isinstance(claim_id, str) or not claim_id.strip():
            raise PacketRejected("CLAIM_ID_REQUIRED")
        result = self._temporal_ledger_call(
            self._lib.surge_temporal_question_get, (self._handle, claim_id.encode()),
            "TEMPORAL_QUESTION_BUFFER_TOO_SMALL")
        return result.get("questions", [])

    @property
    def temporal_question_count(self) -> int:
        return int(self._lib.surge_temporal_question_count(self._handle))

    def get_question_aspect(self, aspect_id: str) -> dict[str, Any]:
        """Return the durable lifecycle status for a QuestionAspect."""
        if not isinstance(aspect_id, str) or not aspect_id.strip():
            raise PacketRejected("QUESTION_ASPECT_ID_REQUIRED")
        return self._temporal_ledger_call(
            self._lib.surge_question_aspect_get_status,
            (self._handle, aspect_id.encode()),
            "QUESTION_ASPECT_BUFFER_TOO_SMALL")


    def get_question_aggregate(self, question_id: str) -> dict[str, Any]:
        """Return a read-only aggregate derived from all durable QuestionAspects."""
        if not isinstance(question_id, str) or not question_id.strip():
            raise PacketRejected("QUESTION_ID_REQUIRED")
        return self._temporal_ledger_call(
            self._lib.surge_question_get_aggregate_status,
            (self._handle, question_id.encode()),
            "QUESTION_AGGREGATE_BUFFER_TOO_SMALL")


    def get_question_reevaluation_candidates(self, question_id: str,
                                             max_depth: int = 8,
                                             max_nodes: int = 256) -> dict[str, Any]:
        """Discover dependent Questions without changing their lifecycle state."""
        if not isinstance(question_id, str) or not question_id.strip():
            raise PacketRejected("QUESTION_ID_REQUIRED")
        if not isinstance(max_depth, int) or isinstance(max_depth, bool) or not 1 <= max_depth <= 64:
            raise PacketRejected("QUESTION_REEVALUATION_DEPTH_INVALID")
        if not isinstance(max_nodes, int) or isinstance(max_nodes, bool) or not 1 <= max_nodes <= 4096:
            raise PacketRejected("QUESTION_REEVALUATION_NODE_LIMIT_INVALID")
        return self._temporal_ledger_call(
            self._lib.surge_question_get_reevaluation_candidates,
            (self._handle, question_id.encode(), max_depth, max_nodes),
            "QUESTION_REEVALUATION_BUFFER_TOO_SMALL")

    def set_question_aspect_status(self, aspect_id: str, status: str,
                                   expected_state_version: int | None = None) -> int:
        """Set OPEN/RESOLVED/DEFERRED using optimistic state-version control."""
        if not isinstance(aspect_id, str) or not aspect_id.strip():
            raise PacketRejected("QUESTION_ASPECT_ID_REQUIRED")
        if status not in ("OPEN", "RESOLVED", "DEFERRED"):
            raise PacketRejected("QUESTION_ASPECT_STATUS_INVALID")
        expected = self.state_version if expected_state_version is None else int(expected_state_version)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_question_aspect_set_status(
            self._handle, aspect_id.encode(), status.encode(), expected, error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if not ok or detail:
            raise PacketRejected(detail or "QUESTION_ASPECT_STATUS_UPDATE_REJECTED")
        return self.state_version

    def route_contradiction(self, relation_id: str,
                            policy_version: str = "question-router-v1") -> dict[str, Any]:
        """Create or retrieve the durable Question route for an accepted contradiction."""
        if not isinstance(relation_id, str) or not relation_id.strip():
            raise PacketRejected("CONTRADICTION_RELATION_ID_REQUIRED")
        if not isinstance(policy_version, str) or not policy_version.strip():
            raise PacketRejected("QUESTION_POLICY_VERSION_REQUIRED")
        before_version = self.state_version
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(256)
        fn = self._lib.surge_question_route_contradiction
        ok = fn(self._handle, relation_id.encode("utf-8"), policy_version.encode("utf-8"),
                None, 0, ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != "QUESTION_ROUTE_BUFFER_TOO_SMALL":
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "QUESTION_ROUTE_READ_FAILED")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(256)
        ok = fn(self._handle, relation_id.encode("utf-8"), policy_version.encode("utf-8"),
                output, len(output), ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "QUESTION_ROUTE_FAILED")
        result = json.loads(output.value.decode("utf-8"))
        # The sizing probe may have committed the route; report whether it
        # existed before this method call rather than the retry's duplicate bit.
        if int(result.get("state_version", before_version)) > before_version:
            result["duplicate"] = False
        return result

    def route_contradiction_scoped(self, relation_id: str, scope_key: str,
                                   policy_version: str = "question-router-v2") -> dict[str, Any]:
        """Group contradictions by unordered Claim pair and explicit scope; retain each event as an aspect."""
        if not isinstance(relation_id, str) or not relation_id.strip(): raise PacketRejected("CONTRADICTION_RELATION_ID_REQUIRED")
        if not isinstance(scope_key, str) or not scope_key.strip(): raise PacketRejected("QUESTION_SCOPE_KEY_REQUIRED")
        if not isinstance(policy_version, str) or not policy_version.strip(): raise PacketRejected("QUESTION_POLICY_VERSION_REQUIRED")
        before_version = self.state_version; required = ctypes.c_size_t(0); error = ctypes.create_string_buffer(256)
        fn = self._lib.surge_question_route_contradiction_scoped
        args = (self._handle, relation_id.encode("utf-8"), policy_version.encode("utf-8"), scope_key.encode("utf-8"))
        ok = fn(*args, None, 0, ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != "QUESTION_ROUTE_BUFFER_TOO_SMALL": raise PacketRejected(error.value.decode("utf-8", errors="replace") or "QUESTION_ROUTE_READ_FAILED")
        output = ctypes.create_string_buffer(required.value); error = ctypes.create_string_buffer(256)
        ok = fn(*args, output, len(output), ctypes.byref(required), error, len(error))
        if not ok: raise PacketRejected(error.value.decode("utf-8", errors="replace") or "QUESTION_ROUTE_FAILED")
        result = json.loads(output.value.decode("utf-8"))
        if int(result.get("state_version", before_version)) > before_version: result["duplicate"] = False
        return result
    def get_focus(self) -> dict[str, Any] | None:
        result = self._temporal_ledger_call(
            self._lib.surge_context_graph_focus_get, (self._handle,),
            "FOCUS_BUFFER_TOO_SMALL")
        return result if result.get("found") else None

