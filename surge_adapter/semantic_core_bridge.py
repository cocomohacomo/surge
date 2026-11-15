"""ctypes access to the journal-backed C++ ContextGraph SemanticDelta API."""
from __future__ import annotations
import ctypes
from typing import Any
from surge_adapter.packet_ingest import CppCoreBridge, PacketRejected, normalize_source

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
        lib.surge_context_graph_apply.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply.restype = ctypes.c_int

    @property
    def scope_count(self) -> int:
        return int(self._lib.surge_context_graph_scope_count(self._handle))

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
