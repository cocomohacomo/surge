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
