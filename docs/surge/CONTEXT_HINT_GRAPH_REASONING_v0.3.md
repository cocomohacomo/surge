# ContextHint Graph Reasoning v0.3

## Purpose

This increment makes graph-derived ContextHint construction consistent under
concurrent writes and ensures Focus and dependency propagation actually drive
bounded graph expansion.

## Snapshot consistency

The builder obtains lexical matches and the full graph snapshot through two
read-only Core calls. It accepts them only when both report the same
`state_version`. It retries the pair up to three times; if the graph changes
during every attempt, it fails with
`CONTEXT_HINT_GRAPH_SNAPSHOT_CHANGED`. It never combines Focus from one
version with lexical matches from another.

## Focus-driven traversal

The current Focus Cluster and Interest are both candidate sources and BFS
seeds when present in the retrieved graph. This makes their typed references
and relation edges discoverable within the configured depth budget.

## Relevant unresolved scopes

An unresolved ScopeSpan is no longer added merely because it exists somewhere
in the journal-backed graph. It becomes eligible when it is a lexical seed or
is reached from a lexical/Focus seed through typed references or relation
edges. Open QuestionAspects follow the same relevance boundary. This avoids
globally injecting unrelated open records into every ContextHint.

## QuestionAspect traversal barrier

A RESOLVED QuestionAspect remains durable historical data, but traversal stops
at that node. It cannot serve as a bridge to reintroduce stale context from
records connected only through the resolved aspect.

## Dependency propagation

Dependency predicates are still evaluated in bounded passes. Every newly
triggered dependent is added to the changed set and candidate set. After
dependency evaluation, traversal restarts from newly discovered dependents,
using the remaining global node budget and the configured depth limit. Their
own claims, evidence, and multi-hop relation lineage are therefore considered.
Each dependency fires at most once per build.

## Failure and budget behavior

- Mixed-version reads after three attempts fail closed.
- Snapshot overflow remains an explicit error.
- Initial and dependency-triggered traversal share one visited-node budget.
- Candidate canonicalization remains deterministic and is applied before
  ContextHint reference budgeting.

## Conformance additions

Tests cover Focus as an actual traversal seed, exclusion of unrelated
unresolved ScopeSpan records, dependency-triggered traversal restart, and
rejection of mixed-version graph reads.

## Deliberate limits

The two retrieval calls are not a server-side transaction. Equal
`state_version` values provide the consistency proof for this append-only
graph version model. A future Core ABI may expose a single atomic
query-plus-snapshot operation to remove the retry window entirely.
