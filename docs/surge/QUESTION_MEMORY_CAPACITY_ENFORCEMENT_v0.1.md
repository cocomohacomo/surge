# Question Memory Capacity Enforcement v0.1

Capacity enforcement bounds ACTIVE Questions without deleting semantic history.

The caller supplies one normalized retention score in [0,1] for every currently ACTIVE Question and a maximum active count (0..4096). Missing, extra, duplicate, unknown, non-active, or out-of-range entries are rejected. The Core sorts by score descending, then Question ID ascending for deterministic ties. It keeps the highest-scoring Questions ACTIVE and atomically changes the remainder to DORMANT in one EventJournal transaction.

Existing DORMANT and ARCHIVED Questions are not promoted or changed. The caller can obtain scores through the retention recommendation API; this capacity API does not infer recency or Focus relevance. A failed transaction changes no in-memory state. QuestionAspect statuses, Claims, Evidence, Relations, and provenance are untouched. Optimistic concurrency uses expected_state_version.

Python API: enforce_question_memory_capacity(retention_scores, max_active, expected_state_version=None).
C ABI: surge_question_memory_enforce_capacity.
The operation is bounded to 4096 active Questions.
