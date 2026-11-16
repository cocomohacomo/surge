# Persistent Context Retrieval v0.1

## Purpose

Provide read-only, deterministic retrieval over the typed ContextGraph after
journal replay. The retrieval layer searches durable Cluster, Claim, and
ScopeSpan records through the Core C ABI. It does not maintain a second
authoritative store and does not mutate Context State.

## Authority and lifecycle

- EventJournal remains the source of truth.
- Core open/recovery replays committed packet and SemanticDelta events before
  retrieval is available.
- Every search reads the recovered ContextGraph and returns its
  `state_version`; the version identifies the graph snapshot used for the
  result.
- Closing and reopening a Core handle against the same journal reconstructs
  the same searchable records.
- Retrieval is read-only and never advances the state version.

A journal is the isolation boundary in this first implementation. Until
ContextGraph records carry an explicit context identifier, applications must
use a separate journal per Context. The retrieval API does not claim
cross-Context authorization or filtering.

## API

C ABI:

```c
int surge_context_graph_retrieve(
    void* handle,
    const char* query,
    const char* kinds_csv,
    size_t limit,
    char* output,
    size_t output_capacity,
    size_t* required_capacity,
    char* error,
    size_t error_capacity);
```

Python:

```python
from surge_adapter.context_retrieval import ContextRetriever

with SemanticCoreBridge(journal_path) as core:
    result = ContextRetriever(core).search(
        "budget", kinds=["CLUSTER", "CLAIM", "SCOPE_SPAN"], limit=20
    )
```

The result contains `state_version`, the original `query`, `items`, and
`total_matches`. A matching graph node includes its typed references and
direct relation edges. A matching ScopeSpan includes its kind, opener text,
closed/unresolved status, effect, target/antecedent/packet references,
source digest, offset basis, and unconditional-claim safety flag.

Supported filters: CLUSTER, ENTITY, EVIDENCE, CLAIM, QUESTION,
QUESTION_ASPECT, DEPENDENCY, INTEREST, SCOPE_SPAN. An omitted/empty filter
searches all supported record types. Each page limit is 1..1000. The paginated C ABI accepts an offset and returns
next_offset; the original non-paginated C ABI remains as a compatibility wrapper
for offset zero. The Python search_all helper traverses pages up to an explicit
result budget (default 100,000), verifies that every page has the same
state_version and total_matches, and rejects mixed-version or incomplete results.
The C ABI reports the required output-buffer size, allowing the Python adapter
to retry safely.

## v0.1 ranking

- Exact full-field match: 100
- Query substring match: 85
- All whitespace-separated query terms found: 65 + term count
- Partial whitespace-separated term match: 20 + proportional term coverage
- Japanese non-whitespace query fallback: 25..60 based on UTF-8 code-point bigram overlap
- No match: excluded

Results sort by score descending, then kind ascending, then immutable ID
ascending. This is a lexical baseline, not semantic similarity. Japanese queries first use exact substring matching, then use a deterministic
UTF-8 code-point bigram fallback. This can recover a phrase when a particle or
modifier appears between query terms (for example, 予算配分 against 予算の配分案).
The fallback is lexical approximation only: it does not provide morphological
analysis, synonym expansion, or embedding-based recall.

## Consistency and failure behavior

- Search uses the in-memory graph that was reconstructed from committed
  journal events, not a process-local history cache.
- The returned state version lets callers detect whether a later operation
  used a different graph snapshot.
- Invalid query, filter, limit, or handle is rejected.
- Insufficient output capacity returns RETRIEVAL_BUFFER_TOO_SMALL and the
  required byte count; no partial JSON is returned.
- Results are bounded by the caller's limit but report total_matches.

## ContextHint and durable RetrievalTicket integration v0.1\n\n- ContextHint candidate priority is exposed through `ContextRetriever.build_hint` and mirrors the C++ `ContextHintBuilder` order: OpenScope, RequiredQuestionAspect, TriggeredDependency, RequiredLineage, ActiveFocusScope, then optional classes.\n- Mandatory overflow produces a RetrievalTicket bound to Context ID, packet reference, source-range digest, policy version, state version, and omitted-reference digest. Optional-only truncation is reported but does not create a ticket.\n- `issue_ticket` and `resolve_ticket` write typed ticket events into the same EventJournal used by packet and graph transactions. They therefore share durable state-version ordering.\n- Core startup replays ticket issue/resolution records. A resolved ticket can be queried after process restart. Re-evaluation at a newer state version requires the caller to declare the query replayable and supply freshly reevaluated references.\n- A stale result is not persisted as a terminal resolution, allowing a later replayable re-evaluation.\n\n## Deliberate limits / next increments

This version does not yet implement:
- semantic/vector retrieval, Japanese morphological analysis, BM25/IDF, or
  query expansion;
- time/source/packet/lineage filters, confidence thresholds, or policy-aware
  mandatory candidate ordering from ContextHint;
- multi-hop graph expansion, contradiction-aware reranking, or dependency
  impact traversal;
- semantic/vector retrieval, Japanese morphological analysis, BM25/IDF, or query expansion;
- explicit Context IDs on graph records, authorization, retention policies,
  or multi-process writer coordination;
- an incremental persistent inverted index. The journal-replayed graph is
  scanned deterministically on each request.

The next stage should add a query AST and ContextHint-compatible candidate
policy, then durable RetrievalTicket records bound to query digest and
state-version semantics. An index should be introduced only as a rebuildable
acceleration layer after correctness and conformance are established.


## v0.2: Offset pagination and Japanese character n-grams

The paginated C ABI operation is
surge_context_graph_retrieve_page(handle, query, kinds_csv, offset, limit, ...).
It preserves the existing ranking order and applies offset only after the
complete deterministic result set has been ranked. Responses include offset,
next_offset (or null on the final page), total_matches, and state_version.

The legacy surge_context_graph_retrieve function remains source- and
binary-compatible and delegates to page retrieval with offset zero.

ContextRetriever.search accepts offset. ContextRetriever.search_all walks
pages and requires a stable state_version and total_matches across all reads.
It fails closed if the graph changes mid-pagination, if a page is empty before
the terminal offset, or if the configured max_items budget would be exceeded.
The default result budget is 100,000 records; applications can set a smaller or
larger explicit budget (up to 1,000,000).

For Japanese text, the lexical scorer adds a fallback based on UTF-8
code-point bigrams after exact-substring and whitespace-token matching fail.
This is intentionally a small deterministic baseline, not a morphological
analyzer. Scores remain lower than exact and complete-token matches, allowing
applications to distinguish approximate recall from direct lexical evidence.

Conformance covers retrieval across 1,105 records, stable multi-page ordering,
and Japanese particle-gap matching. Search remains read-only and all pages
report the graph state version used for ranking.

