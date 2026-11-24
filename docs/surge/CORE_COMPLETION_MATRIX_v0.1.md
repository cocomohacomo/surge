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
| Focus observation integration with completed cognition Snapshot/Diff | Not yet implemented as a unified contract | Design and implementation stage below |
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
   traversal configuration/depth, and deterministic observation digest.
3. A completed snapshot must contain the full logical Focus roots, all
   processed roots, and no deferred roots.
4. Repeating an identical completed snapshot request is idempotent.
5. A changed graph version or Focus digest is rejected as stale; it must not
   be silently rebased.
6. Diff compares only compatible completed snapshots. Incompatible provenance
   yields a stable error rather than a misleading change list.
7. Snapshot and Diff remain detached read-only outputs; neither mutates Focus,
   clusters, relations, cursor, or resource budget.

### First implementation slice

- Define a value DTO for Focus evaluation snapshot metadata and digest.
- Add a pure validator that rejects incomplete, invalidated, or internally
  inconsistent observations.
- Add conformance tests for partial/deferred rejection, complete acceptance,
  stale version and Focus digest, duplicate snapshot identity, and Diff
  provenance mismatch.
- Only after this contract and tests pass, connect the DTO to the existing
  Snapshot/Diff storage API. Do not add a parallel graph store.

### Exit criteria

The slice is complete only when the validator/API, tests, documentation, CI,
and compatibility review are present. Contract-only success is not runtime
completion. The current PR #148 stays Draft and unmerged until its own
conformance and review requirements are satisfied.
