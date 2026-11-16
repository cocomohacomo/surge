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
