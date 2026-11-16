# Question Forgetting and Memory Lifecycle v0.1

Forgetting reduces active accessibility, not durable semantic history. This version applies to Question nodes.

Retention score = 0.30 contradiction strength + 0.20 Focus relevance + 0.25 unresolved importance + 0.10 recency + 0.15 recurrence. Callers supply normalized factors in [0,1]; no machine clock or hidden Focus inference is used. A score below the supplied threshold recommends DORMANT, otherwise ACTIVE. Contradiction strength and unresolved importance outweigh recency; low Focus relevance represents distance from current attention.

- ACTIVE: ordinary retrieval and attention.
- DORMANT: excluded from ordinary active retrieval, but durable and explicitly recallable.
- ARCHIVED: long-term history, excluded from ordinary retrieval. Explicit caller decision only.

Memory state is independent of QuestionAspect OPEN/RESOLVED/DEFERRED. Forgetting never resolves a contradiction, edits Claims, deletes Evidence, removes relations, or changes Focus. ACTIVE is explicit recall. Writes use expected_state_version and EventJournal replay. Reads and recommendations are read-only. Stale writers receive STATE_VERSION_CONFLICT. No automatic state mutation occurs.

Python APIs: recommend_question_memory(...), get_question_memory_state(question_id), set_question_memory_state(question_id,state,expected_state_version=None).
C ABI: surge_question_memory_recommendation, surge_question_memory_get_state, surge_question_memory_set_state.
