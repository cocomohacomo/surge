# Question Memory Capacity Enforcement v0.1

Capacity enforcement bounds ACTIVE Questions without deleting semantic history.

The caller supplies one normalized retention score in [0,1] for every currently ACTIVE Question and a maximum active count (0..4096). Missing, extra, duplicate, unknown, non-active, or out-of-range entries are rejected. The Core sorts by score descending, then Question ID ascending for deterministic ties. It keeps the highest-scoring Questions ACTIVE and atomically changes the remainder to DORMANT in one EventJournal transaction.

Existing DORMANT and ARCHIVED Questions are not promoted or changed. The caller can obtain scores through the retention recommendation API; this capacity API does not infer recency or Focus relevance. A failed transaction changes no in-memory state. QuestionAspect statuses, Claims, Evidence, Relations, and provenance are untouched. Optimistic concurrency uses expected_state_version.

Python API: enforce_question_memory_capacity(retention_scores, max_active, expected_state_version=None).
C ABI: surge_question_memory_enforce_capacity.
The operation is bounded to 4096 active Questions.


## Core-derived retention planning

plan_question_memory_capacity(max_active) returns a read-only plan from the current graph snapshot. The score is:

- 0.30 contradiction strength, detected from linked durable contradiction records
- 0.20 current Focus relevance, when the focused Interest references the Question's Aspect
- 0.25 unresolved importance: OPEN=1.0, DEFERRED=0.65, RESOLVED=0.15; no Aspects=0.5
- 0.10 recency, fixed at 0.5 because this Core path does not infer timestamps or wall-clock time
- 0.15 reference frequency, based on persisted reference events (count / 5, capped at 1.0)

A Question participating in a DEPENDS_ON edge receives a retention floor of 0.75. This is a conservative protection heuristic, not proof that the dependency is currently active. Output includes the state version, factor breakdown, sorted scores, and demotion candidates. Planning does not mutate state. Review the plan, then pass its scores and state version to the existing atomic capacity-enforcement API. Reference counts are durable event-journal data. `record_question_reference(question_id)` increments the count and reactivates a DORMANT Question in the same atomic event. ARCHIVED Questions reject references and require an explicit lifecycle decision to restore.


## Dependency-aware reactivation

When a Question is referenced, `record_question_reference_with_dependents` may traverse reverse `DEPENDS_ON` edges and reactivate dormant dependent Questions. The root reference count and all dependent reactivations are committed in one EventJournal transaction. Traversal is deterministic, bounded by depth (1..64) and node count (1..4096), and fails closed before mutation on overflow or stale state version. Archived Questions are never implicitly reactivated, though traversal may continue through them to discover downstream dependents. Dependency reactivation does not increment dependent reference counts. Replay restores the exact active/dormant state.


## Read-only Question context restoration

`get_question_context_bundle(question_id, max_depth=8, max_nodes=256)` builds a bounded read-only context view. It includes the root and dependent Questions, QuestionAspects that reference those Questions, Claims and Evidence connected by explicit references or graph relations, and relations whose endpoints are both included. It does not reactivate or rewrite any node, resolve QuestionAspects, or change Claim validity. Archived Questions remain visible with their lifecycle state. Limits fail closed and the result carries the State Version for optimistic follow-up operations.


## Evidence-based Question reevaluation proposals

`plan_question_reevaluation(question_id, max_depth=8, max_nodes=256)` returns a deterministic, bounded, read-only proposal for QuestionAspect lifecycle reevaluation. It traverses the root Question and reverse-dependent Questions, then associates QuestionAspects and Claims through shared ABOUT-linked topic clusters. Supporting Evidence is counted through SUPPORTS relations; CONTRADICTS, CONFLICTS_WITH, and RETRACTS relations are treated as conflict signals.

A proposal becomes RESOLVED only when at least one ACTIVE Claim has explicit SUPPORTS Evidence and no conflict relation touches the associated Claims. Any conflict proposes OPEN. Without sufficient evidence, the current status is preserved. The plan reports the current State Version, reason codes, supporting/conflicting counts, associated Claim IDs, and bounded node count.

This API never applies its proposals. It does not change QuestionAspect status, Claim status or validity, Question memory state, Focus, or EventJournal state. A later explicit apply operation must validate the proposal's State Version and re-check the evidence in the Core transaction. Traversal and selected context are bounded by max_depth (1..64) and max_nodes (1..4096); overflow fails closed.
