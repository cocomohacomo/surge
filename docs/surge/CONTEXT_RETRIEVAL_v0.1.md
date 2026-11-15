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
searches all supported record types. Limit is 1..1000. The C ABI reports the
required output-buffer size, allowing the Python adapter to retry safely.

## v0.1 ranking

- Exact full-field match: 100
- Query substring match: 85
- All whitespace-separated query terms found: 65 + term count
- Partial whitespace-separated term match: 20 + proportional term coverage
- No match: excluded

Results sort by score descending, then kind ascending, then immutable ID
ascending. This is a lexical baseline, not semantic similarity. Japanese
queries without whitespace are evaluated as exact substrings; this avoids
inventing a tokenizer but does not provide morphological or embedding-based
recall.

## Consistency and failure behavior

- Search uses the in-memory graph that was reconstructed from committed
  journal events, not a process-local history cache.
- The returned state version lets callers detect whether a later operation
  used a different graph snapshot.
- Invalid query, filter, limit, or handle is rejected.
- Insufficient output capacity returns RETRIEVAL_BUFFER_TOO_SMALL and the
  required byte count; no partial JSON is returned.
- Results are bounded by the caller's limit but report total_matches.

## Deliberate limits / next increments

This version does not yet implement:
- semantic/vector retrieval, Japanese morphological analysis, BM25/IDF, or
  query expansion;
- time/source/packet/lineage filters, confidence thresholds, or policy-aware
  mandatory candidate ordering from ContextHint;
- multi-hop graph expansion, contradiction-aware reranking, or dependency
  impact traversal;
- cursor pagination or immutable RetrievalTicket persistence/resolution;
- explicit Context IDs on graph records, authorization, retention policies,
  or multi-process writer coordination;
- an incremental persistent inverted index. The journal-replayed graph is
  scanned deterministically on each request.

The next stage should add a query AST and ContextHint-compatible candidate
policy, then durable RetrievalTicket records bound to query digest and
state-version semantics. An index should be introduced only as a rebuildable
acceleration layer after correctness and conformance are established.
