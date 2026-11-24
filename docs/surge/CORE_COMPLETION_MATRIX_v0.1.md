# SURGE Core Completion Matrix v0.1

Status: implementation audit / next-stage contract
Audit basis: main plus Focus Traversal continuation PR #148 at commit
`4ea2eacdd6342028780953a14aafd5d59248ba60`.
This matrix distinguishes code presence, CI evidence, and remaining integration work.
It is not a claim that every behavior has been empirically validated.

## Core invariant

The Core's two primitives remain cognition-information clusters and spatial Focus.
Focus defines the logical region. ResourceBudget defines the amount of work
performed in one call. A partial observation must not be presented as a
complete evaluation, and deferred information remains logically in scope.

## Completion matrix

| Capability | Current status | Remaining evidence / work |
|---|---|---|
| Canonical cluster graph and explicit relations | Implemented | Continue schema, replay and conformance coverage |
| Spatial Focus region, placement, per-cluster depth | Implemented | Property tests for geometric boundary and depth independence |
| Logical region independent of work budgets | Implemented | Large-region bounded-memory/latency benchmarks |
| Focus traversal with resumable caller-owned cursor | Implemented on PR #148 | Full CI matrix and API compatibility review |
| Zero-budget no-op and Deferred logical-region preservation | Implemented on PR #148 | CI run #45317191152 and #45317194575 passed for commit 4ea2eac |
| Root- and origin-scoped expansion, deduplicated visible output | Implemented on PR #148 | Adversarial graph/property tests |
| Candidate recall over bounded Focus work | Implemented | Retrieval quality evaluation beyond lexical baseline |
| ProjectionBatch shared typed path / atomic packet+projection route | Implemented in current code/docs | Runtime cross-adapter and real provider contract tests |
| Dependency evaluation inside staged transaction and journal replay | Implemented in current main code | Fault injection and concurrency tests remain |
| Dependency evaluation readout through C ABI and Python bridge | Implemented in current main code | Public API compatibility tests and documentation |
| Completed Focus evaluation Snapshot manifest / codec / durable Store round-trip | Implemented on PR #149 | Cross-platform CI and API review |
| Canonical Focus-independent ContextGraph digest | Implemented on PR #149 | CI and cross-platform verification |
| ContextGraph / Focus manifest consistency | Implemented on PR #149 | Verify snapshot identity at application restore boundary |
| Sliced-vs-unbounded Focus Snapshot equivalence | Implemented on PR #149 | Broader randomized graph/property coverage |
| Snapshot history and temporal Diff | Implemented on PR #149 | Long-run storage/retention policy |
| Context partition identity on graph records/retrieval | Backlog | Explicit identity, migration and isolation tests |
| Retrieval snapshot fixed ceiling / resumable query | Backlog | Replace cap with versioned cursor/query contract |
| Predicate grammar versioning and typed AST | Backlog | AST migration, version compatibility |
| Dependency fixpoint execution for cycles | Backlog | Deterministic bounded convergence contract |
| Cross-block evidence / ScopeSpan projection | Partial | Atomic multi-block anchors with block digests |
| Crash/fault injection and realistic performance | Partial / not measured | journal interruption, restart, races, memory/latency benchmarks |

## Next recommended implementation: completed Focus evaluation snapshots

### Problem

The Focus traversal API returns accumulated observation data and a completion flag,
but cognition Snapshot/Diff is not yet a unified operation that guarantees a
snapshot is created only from a complete Focus evaluation. Consumers could
mistakenly treat a partial page as a finalized cognition state.

### Contract

1. Partial traversal remains an observation only. It cannot create a completed
   evaluation snapshot.
2. Snapshot provenance binds to the graph state version, logical Focus digest,
   graph digest independent of Focus, and deterministic observation digest.
3. A completed snapshot must contain the full logical Focus roots, all
   processed roots, and no deferred roots.
4. Repeating an identical completed snapshot request is idempotent.
5. A changed graph version or Focus digest is rejected as stale; it must not
   be silently rebased.
6. Diff compares completed snapshots in the same Context. It reports Focus
   provenance changes separately from Graph changes. Graph changes are detected
   through the Focus-independent graph digest and root/node/relation ID sets.
7. Snapshot and Diff remain detached read-only outputs; neither mutates Focus,
   clusters, relations, cursor, or resource budget.

### First implementation slice

- [x] Define a value DTO for Focus evaluation snapshot metadata and digest.
- [x] Add a pure validator rejecting incomplete, invalidated, or inconsistent
  observations (logical/processed/deferred roots, provenance/version, duplicate
  nodes/relations, and root/relation endpoints).
- [x] Add regression assertions for partial/deferred rejection, complete
  acceptance, state-version mismatch, missing provenance, and duplicate roots/nodes.
- [x] Add tests for duplicate relations and missing root/relation endpoints.
- [x] Connect completed Focus evaluation manifests to the existing SnapshotDocument / SnapshotCodec / SnapshotStore path without adding a second graph store.
- [x] Implement deterministic root/node/relation ID Diff for snapshots with matching context and Focus provenance.
- [x] Add real SnapshotStore file save/load plus SnapshotCodec restore round-trip.
- [x] Reject truncated and duplicate-ID manifest payloads.
- [x] Report Focus provenance changes separately from Graph digest/ID changes.
- [x] Test Focus-only and Graph-only change classifications.
- [x] Generate a canonical graph digest from ContextGraph nodes and relations,
  excluding Focus geometry/history/depth so the two change domains stay separate.
- [x] Validate a completed Focus manifest against the current ContextGraph
  state version and canonical graph digest.
- [x] Verify sliced traversal and one-shot traversal produce equivalent
  completed Focus manifests.
- [x] Add append-only FocusSnapshotHistory with monotonic watermark and
  context-identity validation plus historical Diff.

### Exit criteria

The implementation slice now includes validator/API, durable manifest persistence,
adversarial manifest validation, independent Focus/Graph change reporting,
ContextGraph-derived graph digest, graph/manifest consistency validation,
sliced-vs-unbounded snapshot equivalence, and append-only history with temporal
Diff. CI evidence must be checked against the current PR head SHA; this does not
imply that PR #149 or its base PR #148 is merged.
