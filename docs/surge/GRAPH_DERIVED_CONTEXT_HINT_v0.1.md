# Graph-Derived ContextHint v0.1

## Purpose

Provide a first automatic candidate-discovery path from the durable ContextGraph
to ContextHint. Existing caller-supplied candidate construction remains
supported and unchanged.

## Operation

`ContextRetriever.build_hint_from_graph(...)` performs the following steps:

1. Search the replayed ContextGraph using the current packet's query text.
2. Convert returned typed records into ContextHint candidates.
3. Promote unresolved ScopeSpan records to `OPEN_SCOPE`.
4. Promote QuestionAspect records to `REQUIRED_QUESTION_ASPECT`.
5. Promote Dependency records with a visible `DEPENDS_ON` edge to
   `TRIGGERED_DEPENDENCY`; otherwise keep them low priority.
6. Promote Claim/Evidence records with visible support/agreement/conflict
   relations to `REQUIRED_LINEAGE`.
7. Classify Cluster records as `RELATED_CLUSTER`.
8. Preserve visible relation endpoints as low-priority references.
9. Delegate ordering, deduplication, budget handling and RetrievalTicket
   generation to the existing ContextHint builder.

## Safety properties

- Closed ScopeSpan records are not promoted as open scopes.
- The policy does not infer a winner from contradictory claims.
- The policy does not create Questions, Interests, Focus changes or graph writes.
- State version is taken from the retrieval snapshot.
- Existing manual candidate API remains available.

## Known limits

This is a conservative first implementation, not complete automatic graph
reasoning. The current retrieval endpoint is lexical and returns only query
matches. Consequently, absence from the result is not evidence that a graph
record does not exist. Focus is not exposed in the retrieval response and is
not inferred. A `DEPENDS_ON` edge is only a structural signal; the current
graph does not expose enough typed activation state to prove that a dependency
condition has fired. QuestionAspect has no resolved/unresolved lifecycle in the
current graph model, so all returned aspects are treated as required candidates.
Multi-hop traversal, temporal re-evaluation, dependency condition evaluation,
and semantic ranking remain future work.

## Conformance

Python tests cover unresolved-vs-closed ScopeSpan handling, QuestionAspect
promotion, relation-gated dependency promotion, deterministic priority order,
and empty-query rejection. Existing C++ graph retrieval and durable ticket
tests remain the underlying integration boundary.
