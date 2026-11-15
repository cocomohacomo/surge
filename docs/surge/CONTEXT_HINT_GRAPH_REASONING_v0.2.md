# ContextGraph Reasoning for ContextHint v0.2

## Scope

This increment completes four capabilities needed by graph-derived ContextHint:
current Focus retrieval, deterministic dependency predicate evaluation,
durable QuestionAspect lifecycle, and bounded multi-hop graph traversal.

## 1. Focus retrieval

The read-only graph retrieval envelope includes the current `focus` object
(`cluster_id`, `interest_id`) or null. It is read from the replayed ContextGraph
state, not inferred from lexical matches or historical focus records.

The automatic hint builder promotes the active Interest to
`ACTIVE_FOCUS_SCOPE` and its Cluster to `RELATED_CLUSTER`. Focus history is
not substituted for current Focus.

## 2. Dependency condition evaluation

A Dependency node continues to use two references:

- `references[0]`: prerequisite
- `references[1]`: dependent

Its `content` field now accepts a deterministic predicate:

| Predicate | Fires when |
|---|---|
| `ON_CHANGE` (also the empty-content default) | prerequisite is in the supplied changed-ref set |
| `PRESENT` | prerequisite exists in the current graph snapshot |
| `CONTENT_EQUALS:<text>` | prerequisite content exactly equals text |
| `CONTENT_CONTAINS:<text>` | prerequisite content contains non-empty text |
| `KIND:<NODE_KIND>` | prerequisite has the named graph node kind |

Unknown predicate strings fail closed and do not fire. The caller supplies
`changed_refs` from the current packet/semantic update. A fired dependency
adds its dependent to the changed set, allowing downstream dependencies to fire
in subsequent bounded passes. Each dependency fires at most once per hint
build. No graph mutation is performed by evaluation.

This predicate grammar is intentionally explicit and deterministic. It is not
a natural-language condition interpreter. More complex temporal, numeric,
negation, and Boolean expressions require a future versioned predicate AST.

## 3. QuestionAspect lifecycle

QuestionAspect status is one of:

- `OPEN`: unresolved and eligible for mandatory ContextHint inclusion
- `RESOLVED`: resolved and excluded from mandatory retrieval
- `DEFERRED`: intentionally postponed; retained only as low-priority context

New QuestionAspect records default to `OPEN`. Status transitions are committed
as `QUESTION_ASPECT_STATUS_SET` EventJournal events, use optimistic
state-version checks, and replay after restart. Repeating the current status is
idempotent. Unknown aspect IDs and stale state versions are rejected.

Python API:

```python
retriever.set_question_aspect_status(
    "aspect-id", "RESOLVED", expected_state_version=core.state_version
)
```

## 4. Multi-hop graph traversal

`build_hint_from_graph` uses a lexical query to establish initial seeds and
a bounded full graph snapshot for expansion. Traversal follows:

- typed node references (including ScopeSpan targets and antecedents)
- graph relation endpoints

The traversal is breadth-first, deterministic, and bounded by `max_depth`
(0..16) and `max_nodes` (1..1000). A budget overflow is an explicit error,
never a silently incomplete result. Node IDs are deduplicated and the strongest
candidate class wins.

Current Focus, unresolved ScopeSpan, and OPEN QuestionAspect are independently
eligible seeds. Resolved aspects are not reintroduced through traversal.
Contradictory claims and their evidence remain separate lineage candidates;
traversal does not select a winner.

## API

```python
hint = retriever.build_hint_from_graph(
    context_id="ctx", packet_ref="packet",
    source_range_digest="<64-hex-sha256>", policy_version="hint-v2",
    query="予算計画", reference_budget=64,
    changed_refs=["claim-17"], max_depth=3, max_nodes=256,
)
```

## Conformance and limits

Tests cover Focus extraction, open/closed/resolved lifecycle filtering,
condition evaluation, transitive dependency firing, multi-hop relation and
reference traversal, deterministic ordering, traversal budgets, status
persistence/replay, idempotent status writes, and stale-version rejection.

The graph snapshot is currently bounded to 1000 records by the retrieval ABI.
Lexical seed discovery remains the baseline ranker. Dependency predicates are
evaluated only when explicitly represented by the grammar above. This version
does not provide arbitrary user-defined code execution, probabilistic
condition inference, or automatic mutation of Focus.
