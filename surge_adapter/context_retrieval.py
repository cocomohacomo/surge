"""Read-only retrieval over the durable, replayed SURGE ContextGraph."""
from __future__ import annotations

import ctypes
import json
import hashlib
from typing import Any

from surge_adapter.packet_ingest import PacketRejected


class _TicketInput(ctypes.Structure):
    _fields_ = [("ticket_id", ctypes.c_char_p), ("context_id", ctypes.c_char_p),
                ("packet_ref", ctypes.c_char_p), ("source_range_digest", ctypes.c_char_p),
                ("omitted_digest", ctypes.c_char_p), ("policy_version", ctypes.c_char_p),
                ("state_version", ctypes.c_uint64),
                ("omitted_refs", ctypes.POINTER(ctypes.c_char_p)),
                ("omitted_count", ctypes.c_size_t)]


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
        self._lib.surge_retrieval_ticket_issue.argtypes = [ctypes.c_void_p, ctypes.POINTER(_TicketInput), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        self._lib.surge_retrieval_ticket_issue.restype = ctypes.c_int
        self._lib.surge_retrieval_ticket_resolve.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p), ctypes.c_size_t, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        self._lib.surge_retrieval_ticket_resolve.restype = ctypes.c_int


    @staticmethod
    def _refs_digest(refs: list[str]) -> str:
        value = 1469598103934665603
        for ref in refs:
            for byte in ref.encode("utf-8"):
                value ^= byte
                value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
            value ^= 0xFF
            value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        return format(value, "x").zfill(16)

    @classmethod
    def build_hint(cls, context_id: str, packet_ref: str, source_range_digest: str,
                   policy_version: str, state_version: int, candidates: list[dict[str, Any]],
                   reference_budget: int) -> dict[str, Any]:
        if (not context_id or not packet_ref or len(source_range_digest) != 64
                or any(c not in "0123456789abcdefABCDEF" for c in source_range_digest)
                or not policy_version or reference_budget <= 0):
            raise ValueError("CONTEXT_HINT_REQUEST_INVALID")
        priority = {
            "OPEN_SCOPE": 0, "REQUIRED_QUESTION_ASPECT": 1,
            "TRIGGERED_DEPENDENCY": 2, "REQUIRED_LINEAGE": 3,
            "ACTIVE_FOCUS_SCOPE": 4, "RELATED_CLUSTER": 5,
            "HISTORICAL_ANALOGUE": 6, "LOW_PRIORITY": 7,
        }
        unique: dict[str, dict[str, Any]] = {}
        for candidate in candidates:
            ref, kind = candidate.get("ref"), candidate.get("kind")
            if not isinstance(ref, str) or not ref or kind not in priority:
                raise ValueError("CONTEXT_HINT_CANDIDATE_INVALID")
            if ref not in unique:
                unique[ref] = candidate
        ordered = sorted(unique.values(), key=lambda c: (
            priority[c["kind"]], -int(c.get("source_overlap", 0)),
            int(c.get("dependency_depth", 0)), c["ref"]))
        mandatory_count = sum(priority[c["kind"]] < 5 for c in ordered)
        included = [c["ref"] for c in ordered[:reference_budget]]
        omitted = [c["ref"] for c in ordered[reference_budget:]]
        by_class: dict[str, int] = {}
        for c in ordered[reference_budget:]:
            by_class[c["kind"]] = by_class.get(c["kind"], 0) + 1
        complete = not omitted or mandatory_count <= reference_budget
        ticket = None
        if mandatory_count > reference_budget:
            ticket_refs = [c["ref"] for c in ordered[reference_budget:mandatory_count]]
            omitted_digest = cls._refs_digest(ticket_refs)
            ticket_key = "|".join((context_id, packet_ref, source_range_digest,
                                   omitted_digest, policy_version))
            ticket = {
                "ticket_id": "rt/" + cls._refs_digest([ticket_key]),
                "context_id": context_id, "packet_ref": packet_ref,
                "source_range_digest": source_range_digest,
                "omitted_digest": omitted_digest, "policy_version": policy_version,
                "state_version": state_version, "omitted_refs": ticket_refs,
            }
            complete = False
        return {
            "state_version": state_version, "context_complete": complete,
            "included_refs": included, "omitted_refs": omitted,
            "omitted_by_class": by_class,
            "included_digest": cls._refs_digest(included),
            "omitted_digest": cls._refs_digest(omitted),
            "retrieval_ticket": ticket,
        }

    def issue_ticket(self, ticket: dict[str, Any]) -> None:
        refs = [str(ref).encode("utf-8") for ref in ticket["omitted_refs"]]
        ref_array = (ctypes.c_char_p * len(refs))(*refs) if refs else None
        value = _TicketInput(
            ticket["ticket_id"].encode(), ticket["context_id"].encode(),
            ticket["packet_ref"].encode(), ticket["source_range_digest"].encode(),
            ticket["omitted_digest"].encode(), ticket["policy_version"].encode(),
            int(ticket["state_version"]),
            ctypes.cast(ref_array, ctypes.POINTER(ctypes.c_char_p)) if ref_array is not None else None,
            len(refs))
        error = ctypes.create_string_buffer(512)
        if not self._lib.surge_retrieval_ticket_issue(
            self._core._handle, ctypes.byref(value), error, len(error)
        ):
            raise PacketRejected(error.value.decode("utf-8", errors="replace")
                                 or "RETRIEVAL_TICKET_ISSUE_FAILED")

    def resolve_ticket(self, ticket_id: str, current_state_version: int,
                       query_replayable: bool = False,
                       reevaluated_refs: list[str] | None = None) -> dict[str, Any]:
        refs = [str(ref).encode("utf-8") for ref in (reevaluated_refs or [])]
        ref_array = (ctypes.c_char_p * len(refs))(*refs) if refs else None
        capacity = 4096
        for _ in range(3):
            output = ctypes.create_string_buffer(capacity)
            required, error = ctypes.c_size_t(0), ctypes.create_string_buffer(512)
            ok = self._lib.surge_retrieval_ticket_resolve(
                self._core._handle, ticket_id.encode(), int(current_state_version),
                1 if query_replayable else 0,
                ctypes.cast(ref_array, ctypes.POINTER(ctypes.c_char_p)) if ref_array is not None else None,
                len(refs), output, capacity, ctypes.byref(required), error, len(error))
            if ok:
                return json.loads(output.value.decode("utf-8"))
            message = error.value.decode("utf-8", errors="replace")
            if message != "RETRIEVAL_BUFFER_TOO_SMALL" or required.value <= capacity:
                raise PacketRejected(message or "RETRIEVAL_TICKET_RESOLVE_FAILED")
            capacity = required.value
        raise PacketRejected("RETRIEVAL_TICKET_RESPONSE_TOO_LARGE")

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
