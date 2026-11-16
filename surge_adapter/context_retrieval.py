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

        # Both lexical seeds and graph expansion must describe one durable
        # graph version. Retry a bounded number of times if a concurrent commit
        # lands between the two read-only ABI calls.
        matched = snapshot = None
        for _ in range(3):
            candidate_matches = self.search(query, limit=limit)
            candidate_snapshot = self.search("*", limit=max_nodes)
            if candidate_matches.get("state_version") == candidate_snapshot.get("state_version"):
                matched, snapshot = candidate_matches, candidate_snapshot
                break
        if snapshot is None:
            raise PacketRejected("CONTEXT_HINT_GRAPH_SNAPSHOT_CHANGED")
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
        seeds: set[str] = set()
        if isinstance(focus, dict):
            focus_cluster = focus.get("cluster_id")
            focus_interest = focus.get("interest_id")
            if isinstance(focus_cluster, str) and focus_cluster:
                add(focus_cluster, "RELATED_CLUSTER", 100)
                if focus_cluster in by_id:
                    seeds.add(focus_cluster)
            if isinstance(focus_interest, str) and focus_interest:
                add(focus_interest, "ACTIVE_FOCUS_SCOPE", 100)
                if focus_interest in by_id:
                    seeds.add(focus_interest)

        for item in matched.get("items", []):
            ref = item.get("id")
            if isinstance(ref, str) and ref in by_id:
                seeds.add(ref)
        # Globally open records are not automatically relevant. They enter
        # as seeds only when the lexical query matched them; otherwise they can
        # still be discovered through a relevant Focus/query graph path.
        for ref in sorted(seeds):
            item = by_id[ref]
            if item.get("kind") == "SCOPE_SPAN" and item.get("status") == "UNRESOLVED":
                add(ref, "OPEN_SCOPE", item.get("score", 0))
            elif item.get("kind") == "QUESTION_ASPECT" and item.get("status", "OPEN") == "OPEN":
                add(ref, "REQUIRED_QUESTION_ASPECT", item.get("score", 0))

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
            # A resolved QuestionAspect may be retained as historical data,
            # but it must not act as a bridge that reintroduces stale context.
            if kind == "QUESTION_ASPECT" and item.get("status", "OPEN") == "RESOLVED":
                continue
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

        # Dependency propagation can reveal nodes after the initial BFS has
        # completed. Re-enter the same bounded traversal from those dependents
        # so their own lineage and multi-hop relations are not silently lost.
        follow_queue = [(ref, 0) for ref in sorted(seeds - set(visited))]
        follow_cursor = 0
        while follow_cursor < len(follow_queue):
            ref, depth = follow_queue[follow_cursor]
            follow_cursor += 1
            if ref in visited and visited[ref] <= depth:
                continue
            if ref not in by_id:
                continue
            if len(visited) >= max_nodes:
                raise PacketRejected("CONTEXT_HINT_GRAPH_TRAVERSAL_BUDGET_EXCEEDED")
            visited[ref] = depth
            item = by_id[ref]
            kind, score = item.get("kind"), item.get("score", 0)
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
                elif item.get("status", "OPEN") == "RESOLVED":
                    continue
            elif kind == "SCOPE_SPAN":
                if item.get("status") == "UNRESOLVED":
                    add(ref, "OPEN_SCOPE", score, depth)
            elif kind != "DEPENDENCY":
                add(ref, "LOW_PRIORITY", score, depth)
            if depth < max_depth:
                for target in sorted(adjacency.get(ref, ())):
                    if target not in visited:
                        follow_queue.append((target, depth + 1))

        # Preserve direct relation endpoints even when traversal did not classify them.
        for ref in list(visited):
            item = by_id[ref]
            for relation in item.get("relations", []):
                target = relation.get("ref")
                if target in by_id:
                    target_item = by_id[target]
                    if (target_item.get("kind") == "QUESTION_ASPECT"
                            and target_item.get("status", "OPEN") == "RESOLVED"):
                        continue
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
    def _hint_priority(kind: str) -> int:
        return {
            "OPEN_SCOPE": 0, "REQUIRED_QUESTION_ASPECT": 1,
            "TRIGGERED_DEPENDENCY": 2, "REQUIRED_LINEAGE": 3,
            "ACTIVE_FOCUS_SCOPE": 4, "RELATED_CLUSTER": 5,
            "HISTORICAL_ANALOGUE": 6, "LOW_PRIORITY": 7,
        }.get(kind, 99)

