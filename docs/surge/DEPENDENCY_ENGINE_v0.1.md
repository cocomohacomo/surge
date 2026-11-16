# SURGE Dependency Engine v0.1

## Dependency direction

A Dependency node has exactly two references:

- references[0]: prerequisite/source whose change can affect another record
- references[1]: dependent/target that must be reconsidered when the prerequisite changes

This direction is explicit and independent of prose relation wording.

## Impact set

Given changed root records, compute the least reachable closure under prerequisite-to-dependent edges. The result includes roots and all transitively affected nodes. Output is stable-sorted by immutable record ID. Duplicate roots/edges do not duplicate output.

The engine enforces a maximum-node budget. If exceeded, it returns IMPACT_BUDGET_EXCEEDED, complete=false, and a partial diagnostic set. Callers MUST NOT treat a partial set as a complete impact set or commit downstream updates from it.

## Strongly connected components

Tarjan's algorithm identifies cyclic SCCs inside the impact set. Components and their members are sorted deterministically. A cyclic SCC is a group requiring coordinated fixpoint evaluation; this module only identifies the SCC and does not itself mutate graph state or assert convergence.

The transaction coordinator must evaluate each SCC against a stable snapshot, iterate deterministic semantic deltas until the state fingerprint stops changing, and enforce an iteration budget. Non-convergence must produce DEFERRED/UNRESOLVED, never an arbitrary partial commit.

## Scope

This is a pure bounded dependency analysis primitive. It does not yet compute source trust, temporal activation, dependency predicates, edge weights, or automatic semantic updates.


## v0.2: Predicate-aware deterministic evaluation

`DependencyEngine::evaluate` evaluates each Dependency predicate against one
immutable graph snapshot. Supported grammar:

- empty or `ON_CHANGE`: prerequisite must be in the changed set;
- `PRESENT`: prerequisite exists in the snapshot;
- `CONTENT_EQUALS:<text>`: exact prerequisite content equality;
- `CONTENT_CONTAINS:<text>`: non-empty substring match;
- `KIND:<NODE_KIND>`: exact prerequisite node-kind match.

A fired dependency adds its dependent to the changed set, enabling transitive
evaluation. Each dependency fires at most once per call, including cycles.
Unknown predicates fail closed and are returned in
`rejected_dependencies`; they never fire. Results are stable-sorted by ID.
Missing roots, malformed edges, and budget overflow fail the evaluation.
Partial results on budget overflow are diagnostic only.

This is a pure evaluator over a caller-provided snapshot. It does not yet
subscribe to EventJournal commits or persist trigger outcomes. The transaction
coordinator must invoke it against the staged post-delta snapshot and persist
its results atomically in a later integration increment.


## v0.3: Strict predicate grammar validation

Predicate prefixes alone are not sufficient to establish a valid predicate.
The evaluator now rejects empty operands for `CONTENT_EQUALS:` and
`CONTENT_CONTAINS:`, empty `KIND:`, and unknown node-kind names. These
records are returned in `rejected_dependencies` and never silently treated as
valid no-match conditions. This keeps malformed configuration distinguishable
from a valid predicate whose condition currently evaluates to false.

Conformance tests cover all four malformed forms. This remains a pure
snapshot evaluator; durable transaction integration is still a separate
required refactor.


## v0.3: transaction-bound dependency evaluation

The Core transaction preparation path now stages the SemanticDelta against a
copy of the current graph, derives changed graph-node references from added
nodes, relation endpoints, ScopeSpan targets/antecedents, and Focus, then
evaluates dependencies against that staged post-delta snapshot.

A successful evaluation is encoded as one `DEPENDENCY_EVALUATION` journal
event and appended to the same event vector as `DELTA_META`, node/relation/
scope/focus events. This means standalone SemanticDelta commits and atomic
RecognitionPacket + SemanticDelta commits both carry dependency outcomes in
the same EventJournal transaction.

The durable record contains the delta ID, triggered dependency IDs and their
prerequisite/dependent endpoints, impacted node IDs, and rejected dependency
IDs. Replay validates every referenced node and dependency endpoint before
restoring the record. Dependency evaluation does not create an external action
or choose a semantic winner.

The transaction preparation logic is shared by `apply` and
`prepare_events`; the latter is the path used by atomic packet admission.
A bounded evaluation budget of 4096 impacted nodes is enforced. If evaluation
fails or exceeds budget, the whole semantic transaction is rejected before
journal commit. Duplicate idempotent deltas do not create a second evaluation
record.

Conformance coverage verifies trigger generation, malformed predicate
rejection, idempotent retry, and restart replay. The C ABI/Python exposure of
these records, fine-grained predicate-operation budgets, and crash/fault
injection remain subsequent work.


## v0.4: durable evaluation read API

The C ABI exposes read-only lookup of a persisted dependency evaluation by
SemanticDelta ID. The response includes triggered dependency IDs and their
prerequisite/dependent endpoints, impacted node IDs, and rejected dependency
IDs. The API uses a bounded output buffer and reports the required capacity
when the caller must retry. Missing records return
`DEPENDENCY_EVALUATION_NOT_FOUND`.

The Python `ContextRetriever.dependency_evaluation(delta_id)` bridge validates
the JSON response shape and retries bounded-buffer responses. The record is
read from the replayed Core graph, not reconstructed by Python. Conformance
coverage reads a record after closing and reopening the Core handle and checks
triggered, impacted, rejected, and missing-record behavior.

This API is read-only. Dependency evaluation remains part of the SemanticDelta
transaction; this endpoint does not trigger reevaluation or mutate state.


## v0.5: typed predicate AST

Predicate parsing is centralized in `DependencyEngine::parse_predicate`.
Expressions are converted into a typed `DependencyPredicate` value with an
explicit kind and operand fields before evaluation. The evaluator now matches
on the parsed kind rather than independently interpreting string prefixes.
Malformed and unknown expressions carry a stable parser error and are
fail-closed into the rejected-dependency result.

Supported variants remain `OnChange`, `Present`, `ContentEquals`,
`ContentContains`, and `Kind`. This is an internal typed representation;
the persisted graph format remains backward-compatible string expressions.
Parser conformance tests cover all valid forms and malformed/unknown forms.

## v0.6: SCC-aware bounded fixed-point evaluation

The engine now exposes `evaluate_fixpoint`, which combines deterministic
impact/SCC analysis with monotonic dependency propagation over one immutable
staged graph snapshot. Dependency IDs are visited in stable map order; a
dependency fires at most once per evaluation, and newly impacted dependents
become eligible in the same or a subsequent round. Level-triggered predicates
(PRESENT/content/kind) contribute their matching prerequisites to the potential
impact roots even when those prerequisites were not in the caller's changed
set. Cyclic strongly connected components are reported explicitly.

The result distinguishes `Converged`, `Deferred`, and `Invalid`. Iteration,
predicate-evaluation, and impacted-node budgets are independently bounded.
Budget exhaustion returns `Deferred`; callers must not persist the partial
trigger set or treat it as a completed evaluation. Invalid roots or malformed
dependency edges return `Invalid`.

The ContextGraph transaction coordinator now uses this bounded evaluator
before journal append. It persists only converged outcomes, in the same atomic
SemanticDelta transaction. If evaluation is deferred or invalid, the staged
transaction is rejected and no partial dependency outcome is journaled.
The current Core policy is 4096 impacted nodes, 4096 rounds, and 1,000,000
predicate evaluations per transaction.

This implementation detects SCCs and guarantees bounded monotonic closure for
the current predicate DSL. It does not yet run arbitrary semantic rewrite
callbacks inside an SCC, nor does it claim convergence for future non-monotonic
rules. Such rules must use explicit state fingerprints and a separate
iteration protocol before being admitted to the Core transaction path.

## v0.7: durable fixed-point diagnostics

`DependencyEvaluationRecord` now persists the evaluation status, iteration
count, predicate evaluation cost, impacted references, triggered/rejected
dependencies, cyclic SCC members, and a stable diagnostic error code.
Successful `CONVERGED` records are appended atomically with their SemanticDelta.

When dependency evaluation returns `DEFERRED` or `INVALID` during a direct
`ContextGraph::apply`, the SemanticDelta is not committed. Instead, a separate
diagnostic-only EventJournal transaction records the attempted evaluation.
This audit transaction advances the journal state version; the returned
`DeltaResult` remains `committed=false` and carries the diagnostic error.
Callers must use the returned state version for any subsequent retry. The
diagnostic can refer to staged nodes that were not committed, so replay
validates its shape and bounds without requiring those staged references to
exist in the durable graph.

A later successful retry using the same delta ID may supersede a prior
DEFERRED/INVALID diagnostic with a CONVERGED record. A prior CONVERGED record
cannot be overwritten. The read-only C ABI and Python
`ContextRetriever.dependency_evaluation` return the extended record, including
status, iteration/cost metrics, SCCs, and error code. Replay also accepts
legacy `DEPENDENCY_EVALUATION` payloads written before this diagnostic format;
legacy records are treated as `CONVERGED` with zero metrics and no SCC/error data.

Atomic packet + SemanticDelta preparation still rejects a deferred evaluation
without appending either packet or semantic state. Its caller receives the
error, but the standalone diagnostic-only audit commit is not made from the
preparation callback, because that callback must remain side-effect-free.
