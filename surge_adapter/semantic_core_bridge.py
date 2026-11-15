"""ctypes access to the journal-backed C++ ContextGraph SemanticDelta API."""
from __future__ import annotations
import ctypes
from typing import Any
from surge_adapter.packet_ingest import CppCoreBridge, PacketRejected

class _Node(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("content", ctypes.c_char_p),
                ("references", ctypes.POINTER(ctypes.c_char_p)),
                ("reference_count", ctypes.c_size_t)]

class _Relation(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("from_id", ctypes.c_char_p), ("to_id", ctypes.c_char_p)]

class SemanticCoreBridge(CppCoreBridge):
    """Packet admission + typed SemanticDelta commit over one shared journal."""

    def __init__(self, journal_path, library_path=None):
        super().__init__(journal_path, library_path)
        lib = self._lib
        lib.surge_context_graph_apply.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply.restype = ctypes.c_int

    def apply_semantic_delta(self, delta: dict[str, Any]) -> dict[str, Any]:
        required = {"delta_id", "idempotency_key", "expected_state_version",
                    "nodes", "relations"}
        if not isinstance(delta, dict) or not required.issubset(delta):
            raise PacketRejected("SEMANTIC_DELTA_SHAPE_INVALID")
        if delta["expected_state_version"] != self.state_version:
            raise PacketRejected("STATE_VERSION_CONFLICT")
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
        focus = delta.get("focus")
        focus_cluster = str(focus["cluster_id"]).encode() if focus else None
        focus_interest = str(focus.get("interest_id", "")).encode() if focus else None
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(1024)
        ok = self._lib.surge_context_graph_apply(
            self._handle, str(delta["delta_id"]).encode(),
            str(delta["idempotency_key"]).encode(),
            int(delta["expected_state_version"]),
            node_array, len(nodes), relation_array, len(relations),
            focus_cluster, focus_interest, ctypes.byref(duplicate),
            ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "SEMANTIC_DELTA_REJECTED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value)}
