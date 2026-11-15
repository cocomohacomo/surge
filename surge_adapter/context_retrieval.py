"""Read-only retrieval over the durable, replayed SURGE ContextGraph."""
from __future__ import annotations

import ctypes
import json
from typing import Any

from surge_adapter.packet_ingest import PacketRejected


class ContextRetriever:
    """Search Cluster, Claim and ScopeSpan records through the Core C ABI.

    The EventJournal remains authoritative. A new Core handle replays committed
    events before this class searches, so results are not process-local caches.
    """

    ALLOWED_KINDS = {
        "CLUSTER", "ENTITY", "EVIDENCE", "CLAIM", "QUESTION",
        "QUESTION_ASPECT", "DEPENDENCY", "INTEREST", "SCOPE_SPAN",
    }

    def __init__(self, core: Any):
        self._core = core
        self._lib = core._lib
        self._lib.surge_context_graph_retrieve.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_size_t, ctypes.POINTER(ctypes.c_char),
            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
        ]
        self._lib.surge_context_graph_retrieve.restype = ctypes.c_int

    def search(self, query: str, kinds: list[str] | None = None,
               limit: int = 20) -> dict[str, Any]:
        if not isinstance(query, str) or not query.strip() or len(query) > 4096:
            raise ValueError("RETRIEVAL_QUERY_INVALID")
        if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 1000:
            raise ValueError("RETRIEVAL_LIMIT_INVALID")
        normalized_kinds = None
        if kinds is not None:
            if not isinstance(kinds, list) or any(
                not isinstance(kind, str) or kind not in self.ALLOWED_KINDS
                for kind in kinds
            ):
                raise ValueError("RETRIEVAL_KIND_FILTER_INVALID")
            normalized_kinds = sorted(set(kinds))
        query_bytes = query.encode("utf-8")
        kinds_bytes = ",".join(normalized_kinds or []).encode("ascii")
        capacity = 65536
        for _ in range(3):
            output = ctypes.create_string_buffer(capacity)
            required = ctypes.c_size_t(0)
            error = ctypes.create_string_buffer(256)
            ok = self._lib.surge_context_graph_retrieve(
                self._core._handle, query_bytes, kinds_bytes, limit,
                output, capacity, ctypes.byref(required), error, len(error)
            )
            if ok:
                try:
                    result = json.loads(output.value.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    raise PacketRejected("RETRIEVAL_RESPONSE_INVALID") from exc
                if not isinstance(result, dict) or not isinstance(result.get("items"), list):
                    raise PacketRejected("RETRIEVAL_RESPONSE_SHAPE_INVALID")
                return result
            message = error.value.decode("utf-8", errors="replace")
            if message != "RETRIEVAL_BUFFER_TOO_SMALL" or required.value <= capacity:
                raise PacketRejected(message or "CONTEXT_RETRIEVAL_FAILED")
            capacity = required.value
        raise PacketRejected("RETRIEVAL_RESPONSE_TOO_LARGE")
