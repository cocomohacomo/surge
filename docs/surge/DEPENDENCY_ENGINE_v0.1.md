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
