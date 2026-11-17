"""Read-only retrieval over the durable, replayed SURGE ContextGraph."""
from __future__ import annotations

import ctypes
import json
import hashlib
import unicodedata
from datetime import datetime
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
        "QUESTION_ASPECT", "DEPENDENCY", "INTEREST", "CHAOS_CLUSTER", "SCOPE_SPAN",
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
        self._lib.surge_context_graph_retrieve_page.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_size_t, ctypes.c_size_t, ctypes.POINTER(ctypes.c_char),
            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
        ]
        self._lib.surge_context_graph_retrieve_page.restype = ctypes.c_int
        self._lib.surge_retrieval_ticket_issue.argtypes = [ctypes.c_void_p, ctypes.POINTER(_TicketInput), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        self._lib.surge_retrieval_ticket_issue.restype = ctypes.c_int
        self._lib.surge_retrieval_ticket_resolve.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p), ctypes.c_size_t, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        self._lib.surge_retrieval_ticket_resolve.restype = ctypes.c_int
        self._lib.surge_question_aspect_set_status.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
        ]
        self._lib.surge_question_aspect_set_status.restype = ctypes.c_int


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
            score = candidate.get("source_overlap", 0)
            depth = candidate.get("dependency_depth", 0)
            if (isinstance(score, bool) or not isinstance(score, int) or score < 0
                    or isinstance(depth, bool) or not isinstance(depth, int) or depth < 0):
                raise ValueError("CONTEXT_HINT_CANDIDATE_METADATA_INVALID")
            current = unique.get(ref)
            if current is None:
                unique[ref] = dict(candidate)
                continue
            # A ref may be discovered through several paths. Keep its strongest
            # semantic class, but aggregate evidence quality independently.
            if priority[kind] < priority[current["kind"]]:
                current["kind"] = kind
            current["source_overlap"] = max(
                int(current.get("source_overlap", 0)), score)
            current["dependency_depth"] = min(
                int(current.get("dependency_depth", depth)), depth)
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
               limit: int = 20, offset: int = 0) -> dict[str, Any]:
        """Return one deterministic page from the current durable graph snapshot."""
        if not isinstance(query, str) or not query.strip() or len(query) > 4096:
            raise ValueError("RETRIEVAL_QUERY_INVALID")
        if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 1000:
            raise ValueError("RETRIEVAL_LIMIT_INVALID")
        if isinstance(offset, bool) or not isinstance(offset, int) or not 0 <= offset <= 10000000:
            raise ValueError("RETRIEVAL_OFFSET_INVALID")
        if kinds is not None:
            if not isinstance(kinds, list) or any(
                not isinstance(kind, str) or kind not in self.ALLOWED_KINDS
                for kind in kinds
            ):
                raise ValueError("RETRIEVAL_KIND_FILTER_INVALID")
            normalized_kinds = sorted(set(kinds))
        else:
            normalized_kinds = None
        query = unicodedata.normalize("NFC", query)
        query_bytes = query.encode("utf-8")
        kinds_bytes = ",".join(normalized_kinds or []).encode("ascii")
        capacity = 65536
        for _ in range(3):
            output = ctypes.create_string_buffer(capacity)
            required = ctypes.c_size_t(0)
            error = ctypes.create_string_buffer(256)
            ok = self._lib.surge_context_graph_retrieve_page(
                self._core._handle, query_bytes, kinds_bytes, offset, limit,
                output, capacity, ctypes.byref(required), error, len(error)
            )
            if ok:
                try:
                    result = json.loads(output.value.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    raise PacketRejected("RETRIEVAL_RESPONSE_INVALID") from exc
                if (not isinstance(result, dict) or
                        not isinstance(result.get("items"), list) or
                        result.get("offset") != offset or
                        "next_offset" not in result):
                    raise PacketRejected("RETRIEVAL_RESPONSE_SHAPE_INVALID")
                return result
            message = error.value.decode("utf-8", errors="replace")
            if message != "RETRIEVAL_BUFFER_TOO_SMALL" or required.value <= capacity:
                raise PacketRejected(message or "CONTEXT_RETRIEVAL_FAILED")
            capacity = required.value
        raise PacketRejected("RETRIEVAL_RESPONSE_TOO_LARGE")

    def search_all(self, query: str, kinds: list[str] | None = None,
                   page_size: int = 500, max_items: int = 100000) -> dict[str, Any]:
        """Retrieve all matches across pages, rejecting mixed-version snapshots."""
        if (isinstance(page_size, bool) or not isinstance(page_size, int)
                or not 1 <= page_size <= 1000):
            raise ValueError("RETRIEVAL_PAGE_SIZE_INVALID")
        if (isinstance(max_items, bool) or not isinstance(max_items, int)
                or not 1 <= max_items <= 1000000):
            raise ValueError("RETRIEVAL_MAX_ITEMS_INVALID")
        first = self.search(query, kinds=kinds, limit=page_size, offset=0)
        items = list(first["items"])
        state_version = first["state_version"]
        total_matches = first["total_matches"]
        next_offset = first["next_offset"]
        while next_offset is not None:
            if len(items) >= max_items:
                raise PacketRejected("RETRIEVAL_RESULT_BUDGET_EXCEEDED")
            page = self.search(query, kinds=kinds, limit=page_size, offset=int(next_offset))
            if (page["state_version"] != state_version or
                    page["total_matches"] != total_matches):
                raise PacketRejected("RETRIEVAL_STATE_VERSION_CHANGED")
            if not page["items"]:
                raise PacketRejected("RETRIEVAL_PAGE_SEQUENCE_INVALID")
            items.extend(page["items"])
            if len(items) > max_items:
                raise PacketRejected("RETRIEVAL_RESULT_BUDGET_EXCEEDED")
            next_offset = page["next_offset"]
        if len(items) != total_matches:
            raise PacketRejected("RETRIEVAL_RESULT_INCOMPLETE")
        return {
            "state_version": state_version,
            "query": unicodedata.normalize("NFC", query),
            "items": items,
            "offset": 0,
            "next_offset": None,
            "total_matches": total_matches,
        }



    def retrieve_focus(self, *, max_members: int = 100,
                       max_associations: int = 1000,
                       diversity_first: bool = True) -> dict[str, Any]:
        """Retrieve typed Focus with score-aware, auditable Chaos selection.

        Missing scores remain UNKNOWN and sort after observed scores. Inactive
        memberships are excluded, not treated as low-scoring candidates.
        """
        for value, low, high, code in (
            (max_members, 1, 1000, "FOCUS_MEMBER_BUDGET_INVALID"),
            (max_associations, 0, 10000, "FOCUS_ASSOCIATION_BUDGET_INVALID"),
        ):
            if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
                raise ValueError(code)
        if not isinstance(diversity_first, bool):
            raise ValueError("FOCUS_DIVERSITY_OPTION_INVALID")

        before = int(self._core.state_version)
        focus = self._core.get_focus()
        if focus is None:
            if int(self._core.state_version) != before:
                raise PacketRejected("RETRIEVAL_STATE_VERSION_CHANGED")
            return {"found": False, "state_version": before, "target": None}

        target_id = focus.get("cluster_id")
        target_kind = focus.get("target_kind", "CLUSTER")
        if not isinstance(target_id, str) or not target_id:
            raise PacketRejected("FOCUS_RESPONSE_INVALID")

        if target_kind == "CHAOS_CLUSTER":
            target = self._core.get_chaos_cluster(target_id)
            members = target.get("members", [])
            if not isinstance(members, list):
                raise PacketRejected("FOCUS_CHAOS_MEMBERS_INVALID")
            if any(not isinstance(member, dict) for member in members):
                raise PacketRejected("FOCUS_CHAOS_MEMBER_INVALID")

            inactive_refs = sorted(str(m.get("member_ref", "")) for m in members
                                   if m.get("membership_active", True) is False)
            active_members = [m for m in members if m.get("membership_active", True) is True]

            def observed_value(record: dict[str, Any], key: str) -> float | None:
                observation = record.get(key)
                if not isinstance(observation, dict):
                    return None
                value = observation.get("value")
                if (isinstance(value, bool) or not isinstance(value, (int, float))
                        or not 0.0 <= float(value) <= 1.0):
                    raise PacketRejected("FOCUS_SCORE_OBSERVATION_INVALID")
                return float(value)

            def score_order(item: dict[str, Any], key: str) -> tuple[int, float, str]:
                value = observed_value(item, key)
                return (1 if value is None else 0,
                        0.0 if value is None else -value,
                        str(item.get("member_ref", "")))

            if diversity_first:
                groups: dict[str, list[dict[str, Any]]] = {}
                for member in active_members:
                    groups.setdefault(str(member.get("admission_reason", "")), []).append(member)
                for group in groups.values():
                    group.sort(key=lambda item: score_order(item, "salience"))
                ordered: list[dict[str, Any]] = []
                depth = 0
                while True:
                    added = False
                    for reason in sorted(groups):
                        if depth < len(groups[reason]):
                            ordered.append(groups[reason][depth])
                            added = True
                    if not added:
                        break
                    depth += 1
            else:
                ordered = sorted(active_members, key=lambda item: score_order(item, "salience"))

            selected = ordered[:max_members]
            selected_refs = {str(item.get("member_ref", "")) for item in selected}
            omitted_members = [str(item.get("member_ref", "")) for item in ordered[max_members:]]

            associations = target.get("associations", [])
            if not isinstance(associations, list):
                raise PacketRejected("FOCUS_CHAOS_ASSOCIATIONS_INVALID")
            if any(not isinstance(item, dict) for item in associations):
                raise PacketRejected("FOCUS_CHAOS_ASSOCIATION_INVALID")
            relevant = [item for item in associations
                        if item.get("from_ref") in selected_refs and item.get("to_ref") in selected_refs]
            by_type: dict[str, list[dict[str, Any]]] = {}
            for association in relevant:
                by_type.setdefault(str(association.get("association_type", "")), []).append(association)

            def association_order(item: dict[str, Any]) -> tuple[int, float, str]:
                value = observed_value(item, "confidence")
                return (1 if value is None else 0,
                        0.0 if value is None else -value,
                        str(item.get("association_id", "")))

            for group in by_type.values():
                group.sort(key=association_order)
            ordered_associations: list[dict[str, Any]] = []
            if diversity_first:
                depth = 0
                while True:
                    added = False
                    for association_type in sorted(by_type):
                        if depth < len(by_type[association_type]):
                            ordered_associations.append(by_type[association_type][depth])
                            added = True
                    if not added:
                        break
                    depth += 1
            else:
                ordered_associations = sorted(relevant, key=association_order)

            selected_associations = ordered_associations[:max_associations]
            omitted_associations = max(0, len(ordered_associations) - max_associations)
            selected_rationale = []
            for rank, member in enumerate(selected, start=1):
                salience = observed_value(member, "salience")
                selected_rationale.append({
                    "member_ref": member.get("member_ref"),
                    "rank": rank,
                    "salience": salience,
                    "salience_status": "OBSERVED" if salience is not None else "UNKNOWN",
                    "admission_reason": member.get("admission_reason"),
                    "selection_basis": "REASON_DIVERSITY_THEN_SALIENCE" if diversity_first
                                       else "SALIENCE_THEN_MEMBER_REF",
                })

            score_context = target.get("observations", {})
            if not isinstance(score_context, dict):
                raise PacketRejected("FOCUS_CLUSTER_OBSERVATIONS_INVALID")
            target = dict(target)
            target["members"] = selected
            target["associations"] = selected_associations
            result = {
                "found": True, "state_version": before,
                "target_kind": "CHAOS_CLUSTER", "focus": focus, "target": target,
                "omitted_member_refs": omitted_members,
                "excluded_inactive_member_refs": inactive_refs,
                "omitted_association_count": omitted_associations,
                "selection_rationale": selected_rationale,
                "score_context": score_context,
                "complete": not omitted_members and omitted_associations == 0,
                "retrieval_policy": "SCORE_AWARE_DIVERSITY_ROUND_ROBIN" if diversity_first
                                    else "SCORE_AWARE_SALIENCE_ORDER",
            }
        elif target_kind == "CLUSTER":
            page = self.search(target_id, kinds=["CLUSTER"], limit=1000)
            target = next((item for item in page.get("items", [])
                           if item.get("id") == target_id), None)
            if target is None:
                raise PacketRejected("FOCUS_TARGET_NOT_FOUND")
            interest = None
            interest_id = focus.get("interest_id")
            if isinstance(interest_id, str) and interest_id:
                interest_page = self.search(interest_id, kinds=["INTEREST"], limit=1000)
                interest = next((item for item in interest_page.get("items", [])
                                 if item.get("id") == interest_id), None)
            result = {
                "found": True, "state_version": before,
                "target_kind": "CLUSTER", "focus": focus,
                "target": target, "interest": interest,
                "omitted_member_refs": [], "omitted_association_count": 0,
                "complete": True, "retrieval_policy": "EXACT_TYPED_TARGET",
            }
        else:
            raise PacketRejected("FOCUS_TARGET_KIND_UNSUPPORTED")

        after = int(self._core.state_version)
        if after != before:
            raise PacketRejected("RETRIEVAL_STATE_VERSION_CHANGED")
        result["state_version"] = after
        return result

    def retrieve_chaos_cluster(
        self, cluster_id: str, *,
        association_types: list[str] | None = None,
        association_statuses: list[str] | None = None,
        valid_at_utc: str | None = None,
        member_admitted_after_utc: str | None = None,
        member_admitted_before_utc: str | None = None,
        producer_ids: list[str] | None = None,
        member_refs: list[str] | None = None,
        include_hypotheses: bool = True,
        include_counterexamples: bool = True,
        lineage_depth: int = 0,
        max_members: int = 1000,
        max_associations: int = 1000,
        max_hypotheses: int = 256,
    ) -> dict[str, Any]:
        """Retrieve a Chaos Cluster using explicit temporal, provenance and lineage filters."""
        if not isinstance(cluster_id, str) or not cluster_id.strip():
            raise ValueError("CHAOS_RETRIEVAL_CLUSTER_ID_INVALID")
        if any(isinstance(v, bool) or not isinstance(v, int) or v < 0
               for v in (lineage_depth, max_members, max_associations, max_hypotheses)):
            raise ValueError("CHAOS_RETRIEVAL_BUDGET_INVALID")
        if lineage_depth > 8 or max_members > 10000 or max_associations > 10000 or max_hypotheses > 1000:
            raise ValueError("CHAOS_RETRIEVAL_BUDGET_INVALID")
        if not isinstance(include_hypotheses, bool) or not isinstance(include_counterexamples, bool):
            raise ValueError("CHAOS_RETRIEVAL_OPTION_INVALID")

        allowed_types = {"CO_OCCURS", "ANALOGY", "HYPOTHESIS", "QUESTION", "CONTRADICTION",
                          "TEMPORAL_NEARNESS", "UNCLASSIFIED", "CREATIVE_SYNTHESIS"}
        allowed_statuses = {"PROVISIONAL", "REVIEWED", "REJECTED", "EXPIRED"}

        def normalize_filter(values: list[str] | None, allowed: set[str], code: str) -> set[str] | None:
            if values is None:
                return None
            if (not isinstance(values, list) or any(not isinstance(v, str) or v not in allowed for v in values)
                    or len(set(values)) != len(values)):
                raise ValueError(code)
            return set(values)

        type_filter = normalize_filter(association_types, allowed_types, "CHAOS_ASSOCIATION_TYPE_FILTER_INVALID")
        status_filter = normalize_filter(association_statuses, allowed_statuses, "CHAOS_ASSOCIATION_STATUS_FILTER_INVALID")

        def normalize_refs(values: list[str] | None, code: str) -> set[str] | None:
            if values is None:
                return None
            if (not isinstance(values, list) or any(not isinstance(v, str) or not v.strip() for v in values)
                    or len(set(values)) != len(values)):
                raise ValueError(code)
            return set(values)

        producer_filter = normalize_refs(producer_ids, "CHAOS_PRODUCER_FILTER_INVALID")
        member_filter = normalize_refs(member_refs, "CHAOS_MEMBER_FILTER_INVALID")
        def validate_utc(value: str | None, code: str) -> None:
            if value is None:
                return
            try:
                if not isinstance(value, str):
                    raise ValueError
                datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
            except ValueError as exc:
                raise ValueError(code) from exc

        validate_utc(valid_at_utc, "CHAOS_VALID_AT_INVALID")
        validate_utc(member_admitted_after_utc, "CHAOS_MEMBER_ADMITTED_AFTER_INVALID")
        validate_utc(member_admitted_before_utc, "CHAOS_MEMBER_ADMITTED_BEFORE_INVALID")
        if (member_admitted_after_utc and member_admitted_before_utc
                and member_admitted_after_utc > member_admitted_before_utc):
            raise ValueError("CHAOS_MEMBER_ADMISSION_RANGE_INVALID")

        before = int(self._core.state_version)
        target = self._core.get_chaos_cluster(
            cluster_id, max_members=10000, max_associations=10000,
            max_hypotheses=1000, max_history_per_association=1000,
            max_hypothesis_refs=1000)
        source_complete = bool(target.get("complete", False))
        source_omitted = target.get("omitted", {})
        members = target.get("members", [])
        associations = target.get("associations", [])
        hypotheses = target.get("pattern_hypotheses", [])
        if (not all(isinstance(values, list) for values in (members, associations, hypotheses))
                or any(not isinstance(item, dict) for values in (members, associations, hypotheses)
                       for item in values)):
            raise PacketRejected("CHAOS_RETRIEVAL_PROJECTION_INVALID")

        def producer_of(item: dict[str, Any]) -> str | None:
            provenance = item.get("provenance")
            if not isinstance(provenance, dict):
                return None
            value = provenance.get("producer_id")
            return value if isinstance(value, str) else None

        filtered_members = [
            item for item in members
            if (member_filter is None or item.get("member_ref") in member_filter)
            and (producer_filter is None or producer_of(item) in producer_filter)
            and (member_admitted_after_utc is None or item.get("admitted_at", "") >= member_admitted_after_utc)
            and (member_admitted_before_utc is None or item.get("admitted_at", "") <= member_admitted_before_utc)
            and item.get("membership_active", True) is True
        ]
        selected_member_refs = {item.get("member_ref") for item in filtered_members}

        filtered_associations = []
        for item in associations:
            association_type = item.get("association_type")
            status = item.get("status")
            if type_filter is not None and association_type not in type_filter:
                continue
            if status_filter is not None and status not in status_filter:
                continue
            if producer_filter is not None and producer_of(item) not in producer_filter:
                continue
            if valid_at_utc is not None:
                valid_from, valid_until = item.get("valid_from"), item.get("valid_until")
                if valid_from and valid_at_utc < valid_from:
                    continue
                if valid_until and valid_at_utc >= valid_until:
                    continue
            if (item.get("from_ref") not in selected_member_refs
                    or item.get("to_ref") not in selected_member_refs):
                continue
            filtered_associations.append(item)

        filtered_hypotheses = []
        if include_hypotheses:
            for item in hypotheses:
                if producer_filter is not None and producer_of(item) not in producer_filter:
                    continue
                if member_filter is not None and not (set(item.get("member_refs", [])) & member_filter):
                    continue
                if not include_counterexamples:
                    item = dict(item)
                    item["counterexample_refs"] = []
                filtered_hypotheses.append(item)

        selected_members = filtered_members[:max_members]
        selected_associations = filtered_associations[:max_associations]
        selected_hypotheses = filtered_hypotheses[:max_hypotheses]
        omitted_member_refs = [str(item.get("member_ref", "")) for item in filtered_members[max_members:]]
        omitted_association_refs = [str(item.get("association_id", "")) for item in filtered_associations[max_associations:]]
        omitted_hypothesis_refs = [str(item.get("hypothesis_id", "")) for item in filtered_hypotheses[max_hypotheses:]]

        # Lineage traversal is metadata-only. It never silently expands the
        # content budget of linked clusters or conflates lineage with membership.
        lineage: list[dict[str, Any]] = []
        unresolved_lineage_refs: list[str] = []
        omitted_lineage_refs: list[str] = []
        visited = {cluster_id}
        frontier = [(target, 0)] if lineage_depth > 0 else []
        while frontier:
            current, depth = frontier.pop(0)
            neighbors = sorted(set(current.get("parent_refs", [])) | set(current.get("child_refs", [])))
            if depth >= lineage_depth:
                omitted_lineage_refs.extend(ref for ref in neighbors if ref not in visited)
                continue
            for ref in neighbors:
                if ref in visited:
                    continue
                visited.add(ref)
                try:
                    meta = self._core.get_chaos_cluster(
                        ref, max_members=0, max_associations=0, max_hypotheses=0,
                        max_history_per_association=0, max_hypothesis_refs=0)
                except PacketRejected:
                    unresolved_lineage_refs.append(ref)
                    continue
                entry = {
                    "cluster_id": ref, "lifecycle": meta.get("lifecycle"),
                    "parent_refs": meta.get("parent_refs", []),
                    "child_refs": meta.get("child_refs", []),
                    "state_version": meta.get("state_version"),
                }
                lineage.append(entry)
                frontier.append((meta, depth + 1))

        after = int(self._core.state_version)
        if after != before:
            raise PacketRejected("RETRIEVAL_STATE_VERSION_CHANGED")

        result_target = dict(target)
        result_target["members"] = selected_members
        result_target["associations"] = selected_associations
        result_target["pattern_hypotheses"] = selected_hypotheses
        result = {
            "found": True, "target_kind": "CHAOS_CLUSTER",
            "cluster_id": cluster_id, "state_version": after,
            "target": result_target,
            "query": {
                "association_types": sorted(type_filter) if type_filter is not None else None,
                "association_statuses": sorted(status_filter) if status_filter is not None else None,
                "valid_at_utc": valid_at_utc,
                "member_admitted_after_utc": member_admitted_after_utc,
                "member_admitted_before_utc": member_admitted_before_utc,
                "producer_ids": sorted(producer_filter) if producer_filter is not None else None,
                "member_refs": sorted(member_filter) if member_filter is not None else None,
                "include_hypotheses": include_hypotheses,
                "include_counterexamples": include_counterexamples,
                "lineage_depth": lineage_depth,
            },
            "lineage": lineage,
            "unresolved_lineage_refs": sorted(set(unresolved_lineage_refs)),
            "omitted_lineage_refs": sorted(set(omitted_lineage_refs)),
            "omitted_member_refs": omitted_member_refs,
            "omitted_association_refs": omitted_association_refs,
            "omitted_hypothesis_refs": omitted_hypothesis_refs,
            "source_omitted": source_omitted,
            "complete": source_complete and not omitted_member_refs and
                        not omitted_association_refs and not omitted_hypothesis_refs and
                        not unresolved_lineage_refs and not omitted_lineage_refs,
        }
        return result

    def set_question_aspect_status(self, aspect_id: str, status: str,
                                    expected_state_version: int | None = None) -> int:
        """Durably transition a QuestionAspect lifecycle state."""
        if not isinstance(aspect_id, str) or not aspect_id:
            raise ValueError("QUESTION_ASPECT_ID_INVALID")
        if status not in {"OPEN", "RESOLVED", "DEFERRED"}:
            raise ValueError("QUESTION_ASPECT_STATUS_INVALID")
        expected = self._core.state_version if expected_state_version is None else int(expected_state_version)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_question_aspect_set_status(
            self._core._handle, aspect_id.encode("utf-8"), status.encode("ascii"),
            expected, error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace")
                                 or "QUESTION_ASPECT_STATUS_UPDATE_FAILED")
        return int(self._core.state_version)

    @staticmethod
    def _dependency_fires(node: dict[str, Any], prerequisite: dict[str, Any],
                          changed: set[str]) -> bool:
        """Evaluate explicit, deterministic dependency predicates.

        Grammar: ON_CHANGE (default), PRESENT, CONTENT_EQUALS:<text>,
        CONTENT_CONTAINS:<text>, KIND:<NODE_KIND>.
        """
        condition = str(node.get("content", "")).strip()
        if not condition or condition == "ON_CHANGE":
            return node["references"][0] in changed
        if condition == "PRESENT":
            return bool(prerequisite)
        if condition.startswith("CONTENT_EQUALS:"):
            return prerequisite.get("content", "") == condition[len("CONTENT_EQUALS:"):]
        if condition.startswith("CONTENT_CONTAINS:"):
            needle = condition[len("CONTENT_CONTAINS:"):]
            return bool(needle) and needle in prerequisite.get("content", "")
        if condition.startswith("KIND:"):
            return prerequisite.get("kind") == condition[len("KIND:"):]
        return False

    def build_hint_from_graph(self, context_id: str, packet_ref: str,
                              source_range_digest: str, policy_version: str,
                              query: str, reference_budget: int,
                              limit: int = 100, changed_refs: list[str] | None = None,
                              max_depth: int = 3, max_nodes: int = 256) -> dict[str, Any]:
        """Build a hint from focus, lifecycle-aware graph state and bounded traversal."""
        if not isinstance(query, str) or not query.strip():
            raise ValueError("CONTEXT_HINT_GRAPH_QUERY_INVALID")
        if (isinstance(max_depth, bool) or not isinstance(max_depth, int)
                or not 0 <= max_depth <= 16
                or isinstance(max_nodes, bool) or not isinstance(max_nodes, int)
                or not 1 <= max_nodes <= 1000):
            raise ValueError("CONTEXT_HINT_GRAPH_BUDGET_INVALID")
        if changed_refs is None:
            changed_refs = []
        if not isinstance(changed_refs, list) or any(not isinstance(x, str) or not x for x in changed_refs):
            raise ValueError("DEPENDENCY_CHANGED_REFS_INVALID")

        matched = self.search(query, limit=limit)
        snapshot = self.search("*", limit=max_nodes)
        items = snapshot.get("items", [])
        by_id = {item["id"]: item for item in items if isinstance(item.get("id"), str)}
        if len(items) >= max_nodes and snapshot.get("total_matches", len(items)) > max_nodes:
            raise PacketRejected("CONTEXT_HINT_GRAPH_SNAPSHOT_BUDGET_EXCEEDED")
        candidate_map: dict[str, dict[str, Any]] = {}
        classes: dict[str, str] = {}

        def add(ref: str, kind: str, score: int = 0, depth: int = 0) -> None:
            if ref not in classes or self._hint_priority(kind) < self._hint_priority(classes[ref]):
                classes[ref] = kind
            else:
                return
            candidate_map[ref] = {"ref": ref, "kind": kind,
                                  "source_overlap": max(0, int(score)),
                                  "dependency_depth": max(0, int(depth))}

        focus = snapshot.get("focus")
        if isinstance(focus, dict):
            focus_cluster = focus.get("cluster_id")
            focus_interest = focus.get("interest_id")
            if isinstance(focus_cluster, str) and focus_cluster:
                add(focus_cluster, "RELATED_CLUSTER", 100)
            if isinstance(focus_interest, str) and focus_interest:
                add(focus_interest, "ACTIVE_FOCUS_SCOPE", 100)

        seeds: set[str] = set()
        for item in matched.get("items", []):
            ref = item.get("id")
            if isinstance(ref, str) and ref in by_id:
                seeds.add(ref)
        for ref, item in by_id.items():
            if item.get("kind") == "SCOPE_SPAN" and item.get("status") == "UNRESOLVED":
                add(ref, "OPEN_SCOPE", item.get("score", 0))
                seeds.add(ref)
            elif item.get("kind") == "QUESTION_ASPECT":
                status = item.get("status", "OPEN")
                if status == "OPEN":
                    add(ref, "REQUIRED_QUESTION_ASPECT", item.get("score", 0))
                    seeds.add(ref)
                elif status == "DEFERRED":
                    add(ref, "LOW_PRIORITY", item.get("score", 0))

        # Bounded breadth-first traversal over typed references and relation edges.
        adjacency: dict[str, set[str]] = {ref: set() for ref in by_id}
        for ref, item in by_id.items():
            linked_refs = list(item.get("references", []))
            linked_refs.extend(item.get("target_refs", []))
            linked_refs.extend(item.get("antecedent_refs", []))
            for target in linked_refs:
                if target in by_id:
                    adjacency[ref].add(target)
                    adjacency[target].add(ref)
            for relation in item.get("relations", []):
                target = relation.get("ref")
                if target in by_id:
                    adjacency[ref].add(target)
                    adjacency[target].add(ref)
        queue = [(ref, 0) for ref in sorted(seeds)]
        visited: dict[str, int] = {}
        cursor = 0
        while cursor < len(queue):
            ref, depth = queue[cursor]
            cursor += 1
            if ref in visited and visited[ref] <= depth:
                continue
            if len(visited) >= max_nodes:
                raise PacketRejected("CONTEXT_HINT_GRAPH_TRAVERSAL_BUDGET_EXCEEDED")
            visited[ref] = depth
            item = by_id[ref]
            kind = item.get("kind")
            score = item.get("score", 0)
            if kind in {"CLAIM", "EVIDENCE"}:
                rel_kinds = {r.get("kind") for r in item.get("relations", [])}
                add(ref, "REQUIRED_LINEAGE" if rel_kinds & {
                    "SUPPORTS", "CONTRADICTS", "AGREES_WITH", "CONFLICTS_WITH"
                } else "LOW_PRIORITY", score, depth)
            elif kind == "CLUSTER":
                add(ref, "RELATED_CLUSTER", score, depth)
            elif kind == "QUESTION_ASPECT":
                if item.get("status", "OPEN") == "OPEN":
                    add(ref, "REQUIRED_QUESTION_ASPECT", score, depth)
            elif kind == "SCOPE_SPAN":
                if item.get("status") == "UNRESOLVED":
                    add(ref, "OPEN_SCOPE", score, depth)
            elif kind not in {"DEPENDENCY"}:
                add(ref, "LOW_PRIORITY", score, depth)
            if depth < max_depth:
                for target in sorted(adjacency.get(ref, ())):
                    if target not in visited:
                        queue.append((target, depth + 1))

        # Dependencies fire only when their explicit predicate evaluates true.
        changed = set(changed_refs)
        fired: set[str] = set()
        for _ in range(max_depth + 1):
            newly_fired = []
            for dep_id, dep in by_id.items():
                if dep.get("kind") != "DEPENDENCY" or dep_id in fired:
                    continue
                refs = dep.get("references", [])
                if len(refs) != 2 or refs[0] not in by_id or refs[1] not in by_id:
                    continue
                if self._dependency_fires(dep, by_id[refs[0]], changed):
                    newly_fired.append((dep_id, refs[1]))
            if not newly_fired:
                break
            for dep_id, dependent in newly_fired:
                fired.add(dep_id)
                changed.add(dependent)
                add(dep_id, "TRIGGERED_DEPENDENCY", 100)
                add(dependent, "REQUIRED_LINEAGE", 100)
                seeds.add(dependent)

        # Preserve direct relation endpoints even when traversal did not classify them.
        for ref in list(visited):
            item = by_id[ref]
            for relation in item.get("relations", []):
                target = relation.get("ref")
                if target in by_id:
                    add(target, "LOW_PRIORITY", item.get("score", 0), visited[ref] + 1)

        candidates = list(candidate_map.values())
        return self.build_hint(
            context_id=context_id, packet_ref=packet_ref,
            source_range_digest=source_range_digest,
            policy_version=policy_version,
            state_version=int(snapshot.get("state_version", self._core.state_version)),
            candidates=candidates, reference_budget=reference_budget,
        )

    @staticmethod
    def changed_refs_from_semantic_delta(delta: dict[str, Any]) -> list[str]:
        """Derive ON_CHANGE triggers from the committed SemanticDelta payload.

        Node IDs are direct changes. Relation endpoints and ScopeSpan targets /
        antecedents are included because their semantic neighborhood changed.
        Focus references are included when Focus itself is updated.
        """
        if not isinstance(delta, dict):
            raise ValueError("SEMANTIC_DELTA_SHAPE_INVALID")
        changed: set[str] = set()
        nodes = delta.get("nodes", [])
        relations = delta.get("relations", [])
        scopes = delta.get("scope_spans", [])
        if not all(isinstance(value, list) for value in (nodes, relations, scopes)):
            raise ValueError("SEMANTIC_DELTA_SHAPE_INVALID")
        for node in nodes:
            if not isinstance(node, dict) or not isinstance(node.get("id"), str) or not node["id"]:
                raise ValueError("SEMANTIC_DELTA_NODE_INVALID")
            changed.add(node["id"])
        for relation in relations:
            if not isinstance(relation, dict):
                raise ValueError("SEMANTIC_DELTA_RELATION_INVALID")
            for key in ("from", "to"):
                ref = relation.get(key)
                if not isinstance(ref, str) or not ref:
                    raise ValueError("SEMANTIC_DELTA_RELATION_INVALID")
                changed.add(ref)
        for scope in scopes:
            if not isinstance(scope, dict) or not isinstance(scope.get("span_id"), str):
                raise ValueError("SEMANTIC_DELTA_SCOPE_INVALID")
            changed.add(scope["span_id"])
            for key in ("target_refs", "antecedent_refs"):
                refs = scope.get(key, [])
                if not isinstance(refs, list) or any(not isinstance(ref, str) or not ref for ref in refs):
                    raise ValueError("SEMANTIC_DELTA_SCOPE_INVALID")
                changed.update(refs)
        focus = delta.get("focus")
        if focus is not None:
            if not isinstance(focus, dict):
                raise ValueError("SEMANTIC_DELTA_FOCUS_INVALID")
            for key in ("cluster_id", "interest_id"):
                ref = focus.get(key)
                if ref:
                    if not isinstance(ref, str):
                        raise ValueError("SEMANTIC_DELTA_FOCUS_INVALID")
                    changed.add(ref)
        return sorted(changed)

    def build_hint_after_semantic_delta(
        self, context_id: str, packet_ref: str, source_range_digest: str,
        policy_version: str, query: str, reference_budget: int,
        delta: dict[str, Any], commit_result: dict[str, Any],
        limit: int = 100, max_depth: int = 3, max_nodes: int = 256,
    ) -> dict[str, Any]:
        """Build a hint using change triggers derived from an accepted Core commit."""
        if not isinstance(commit_result, dict) or commit_result.get("committed") is not True:
            raise PacketRejected("SEMANTIC_DELTA_NOT_COMMITTED")
        if not isinstance(commit_result.get("state_version"), int):
            raise PacketRejected("SEMANTIC_DELTA_COMMIT_VERSION_MISSING")
        if commit_result["state_version"] != self._core.state_version:
            raise PacketRejected("SEMANTIC_DELTA_COMMIT_VERSION_STALE")
        changed_refs = self.changed_refs_from_semantic_delta(delta)
        return self.build_hint_from_graph(
            context_id=context_id, packet_ref=packet_ref,
            source_range_digest=source_range_digest, policy_version=policy_version,
            query=query, reference_budget=reference_budget, limit=limit,
            changed_refs=changed_refs, max_depth=max_depth, max_nodes=max_nodes,
        )

    @staticmethod
    def _hint_priority(kind: str) -> int:
        return {
            "OPEN_SCOPE": 0, "REQUIRED_QUESTION_ASPECT": 1,
            "TRIGGERED_DEPENDENCY": 2, "REQUIRED_LINEAGE": 3,
            "ACTIVE_FOCUS_SCOPE": 4, "RELATED_CLUSTER": 5,
            "HISTORICAL_ANALOGUE": 6, "LOW_PRIORITY": 7,
        }.get(kind, 99)

