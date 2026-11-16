# Question Reevaluation Discovery API v0.1

## Purpose

A change to a prerequisite Question can make dependent Questions worth
reconsidering. This API discovers those candidates through Question-to-Question
DEPENDS_ON edges. It is read-only: discovery is not a resolution decision and
does not mutate QuestionAspect, ClaimValidity, ClaimStatus, Focus, or the
EventJournal.

## Direction and traversal

An edge A DEPENDS_ON B means A is dependent on prerequisite B. Starting from
a changed Question B, the API walks reverse edges to discover direct and
transitive dependents. Each candidate includes its minimum traversal depth,
aggregate Question status at the current graph snapshot, and the relation IDs
on the discovered path. Results are deterministic: depth ascending, then
Question ID ascending.

## Bounds and errors

The C ABI accepts max_depth in 1..64 and max_nodes in 1..4096. The Python
bridge defaults to depth 8 and 256 candidate Questions. Exceeding the node
budget fails closed with QUESTION_REEVALUATION_NODE_LIMIT; it never returns a
silently truncated list. Invalid limits are rejected. Unknown root IDs return
QUESTION_NOT_FOUND. The graph state version is included so callers can detect
stale results before acting on them.

## Authority boundary

The caller decides whether and how to reevaluate returned candidates. This
API does not run recognizers, resolve questions, rank evidence, alter Focus,
or trigger external actions. A candidate with aggregate status RESOLVED is
still only a candidate for reconsideration; it is not reopened automatically.

## API

Python:
`SemanticCoreBridge.get_question_reevaluation_candidates(question_id, max_depth=8, max_nodes=256)`

C ABI:
`surge_question_get_reevaluation_candidates(handle, question_id, max_depth, max_nodes, ...)`

The result contains trigger_question_id, candidates, max_depth, max_nodes, and
state_version. Each candidate contains question_id, status, depth, and
via_relation_ids.
