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
- 0.15 recurrence proxy, based on the number of durable QuestionAspects (saturating at four)

A Question participating in a DEPENDS_ON edge receives a retention floor of 0.75. This is a conservative protection heuristic, not proof that the dependency is currently active. Output includes the state version, factor breakdown, sorted scores, and demotion candidates. Planning does not mutate state. Review the plan, then pass its scores and state version to the existing atomic capacity-enforcement API. The recurrence factor is explicitly an Aspect-count proxy, not an access-frequency measurement.
