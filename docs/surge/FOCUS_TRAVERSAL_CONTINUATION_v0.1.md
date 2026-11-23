# Focus Traversal Continuation Contract v0.1

Status: implementation contract for the Focus-bounded cognition traversal.
Parent: SPECIFICATION_v1.0.md and the current ContextGraph Focus API.
Branch: feat/focus-traversal-resume

## 1. Purpose

This contract defines resumable traversal inside a logical Focus root. It does
not add a second memory store, cognition engine, scoring engine, or classifier.
The canonical information remains in the existing Cluster/ChaosCluster graph.
The continuation cursor is caller-owned execution state, not canonical memory.

The requirement follows from the v1.0 invariants: admitted information remains
cluster-resident; shared members are canonical nodes; budget pressure must not
silently discard information; and resource admission is separate from the
logical information region.

## 2. Required separation

The implementation MUST separate:
- Logical Focus: the complete 3D region and roots selected by spatial
  placement. It is independent of processing budgets.
- Root cursor: position in the deterministic ordered list of logical roots.
- Traversal cursor: active root plus its unexpanded frontier and traversal
  visitation state.
- Work budget: independent node and relation limits for one invocation.
- Observation result: only graph facts actually materialized by the current
  invocation, plus explicit continuation/deferred status.

A budget limit MUST NOT mutate Focus geometry, cluster placement, cluster
classification, membership weights, or cognition content.

## 3. Cursor state

A resumable traversal cursor MUST carry, at minimum:
- committed graph state_version;
- digest of query and all traversal-affecting inputs;
- logical Focus identity/digest, including region, ordered roots, positions,
  and per-root depth configuration;
- next logical root index;
- active root identity;
- deterministic frontier entries (node/cluster identity, origin root,
  traversal level, and any path discriminator required by depth semantics);
- visited expansion keys, scoped by origin root and depth/path semantics;
- relation visitation/inspection state sufficient to avoid charging or
  returning the same relation twice;
- explicit status distinguishing active, deferred, and complete.

The cursor is caller-owned and serializable by the caller. It MUST NOT be
silently persisted as canonical cognition information.

## 4. Traversal and de-duplication semantics

1. Roots are processed in deterministic order.
2. A root is complete only when its frontier is empty.
3. An empty frontier with an unprocessed root is not global completion.
4. Node visibility is deduplicated in the observation, but expansion visitation
   is scoped by origin root and depth semantics. A shared node reached from two
   roots may need separate expansion because the roots may have different
   depth limits.
5. Relation inspection is deduplicated for output and accounting within the
   cursor's declared observation semantics. The implementation must not
   confuse an already emitted relation with all traversal paths through its
   target having been expanded.
6. A candidate is eligible for completed-root recall only after its required
   supporting members are visible under the existing recall contract. Partial
   traversal must not masquerade as a completed cognition candidate.
7. Candidate output caps must not advance the root cursor past an omitted
   candidate. The existing PR #145 pagination guarantee remains mandatory.

## 5. Budget semantics

Node and relation budgets are independent per invocation. A call may stop
because either limit is exhausted. The cursor retains the exact remaining
frontier and resumes on the next call.

The caller may increase or decrease either budget between calls. A smaller
budget may cause an immediate yield without losing cursor state. A larger
budget continues from the retained frontier; it does not restart a completed
root or shrink the logical Focus region.

A budget of zero is a no-op. It must not consume roots or frontier entries.

## 6. Invalidation

The cursor MUST fail closed when any traversal-defining state changes:
- graph state_version mismatch;
- query/input digest mismatch;
- Focus region, spatial placement, root ordering, or depth digest mismatch;
- cursor schema/version mismatch or malformed frontier/visited state.

Budget values are deliberately excluded from traversal identity so that
budget changes do not invalidate a valid cursor.

The implementation must return a typed/stable error and leave the caller's
cursor unchanged on invalidation.

## 7. Completion and result contract

The result must distinguish:
- complete: all logical roots have empty frontiers;
- in_progress: a root has a retained frontier;
- deferred: roots remain unstarted because the invocation budget was spent;
- invalidated: cursor cannot safely resume.

next_root_index alone is insufficient to represent in-root continuation.
Legacy root-only APIs remain source-compatible and retain their existing
behavior; the new continuation path is additive until conformance is proven.

## 8. Required conformance tests

- one root with a large fan-out, resumed over many small node budgets;
- one root with a large relation set, resumed over many small relation budgets;
- independent node/relation exhaustion;
- budget increase and decrease between calls;
- zero-budget no-op;
- no duplicate node/relation output or accounting across pages;
- shared member reached from multiple roots with different depth limits;
- nested Cluster/ChaosCluster traversal and depth zero;
- frontier exhaustion versus unstarted-root deferral;
- candidate cap does not skip candidates (PR #145 regression);
- graph mutation invalidates the cursor;
- query, Focus geometry, placement, root order, or depth changes invalidate it;
- malformed cursor input fails closed without mutation;
- Snapshot/Diff records only completed evaluation states and preserves
  evaluation state_version provenance.

## 9. Non-goals and Core boundary

The Core does not decide whether a candidate is true, relevant, correct, or
actionable. It does not invent cluster positions, classifications, weights,
or recognized content. Projectors supply those values. The Core provides
bounded, deterministic graph observation and preserves caller continuation.

This contract does not authorize a parallel graph, independent candidate
store, hidden semantic scoring, Focus auto-resizing, payload deletion, or
implicit changes to membership weights.

## 10. Implementation sequence

1. Add a versioned traversal-cursor value type and stable validation errors.
2. Extract the existing Focus traversal into a shared engine that accepts and
   updates a detached cursor state, preserving the legacy root-only wrapper.
3. Add cursor-aware focused observation and candidate recall overloads.
4. Add conformance tests above, including shared-node/multi-origin depth.
5. Integrate completed traversal observations with existing cognition
   Snapshot/Diff without recording partial results as completed evaluations.
6. Run the complete repository CI matrix and report any platform gaps.

A design document or passing unit test alone is not implementation completion.
The feature is complete only after the cursor-aware execution path, regression
tests, API compatibility checks, and CI are all verified.