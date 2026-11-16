# Question Relationship Network v0.1

## Relation types

Question nodes support durable graph relations:

- `DUPLICATES`: endpoints represent duplicate or substantially equivalent Questions; consumers should treat the relation as symmetric.
- `CONTAINS`: directed from the broader Question to the narrower Question.
- `AGREES_WITH`: agreement between two Questions.
- `CONFLICTS_WITH`: disagreement or incompatibility between two Questions.
- `DEPENDS_ON`: directed from the dependent Question to its prerequisite.

Existing AGREES_WITH and CONFLICTS_WITH remain valid for Cluster and Claim
pairs. Question-specific DUPLICATES and CONTAINS reject non-Question endpoints.

## Dependency and cycle policy

For Question-to-Question DEPENDS_ON, A -> B means A depends on B. Before
committing a new edge, Core traverses existing Question dependency edges from
B. If A is reachable, the new edge would create a cycle and the entire
SemanticDelta is rejected with QUESTION_DEPENDENCY_CYCLE. Traversal is bounded
to 4096 visited Questions; exceeding that bound rejects the transaction with
QUESTION_DEPENDENCY_TRAVERSAL_LIMIT.

Cycle validation runs against staged transaction state, so cycles formed by
multiple edges in one delta are rejected atomically.

## Lifecycle and reevaluation boundary

Relations are structural context, not resolution commands. They do not resolve
QuestionAspects, mutate ClaimValidity/ClaimStatus, change Focus, or trigger
external actions. A later reevaluation layer may use these edges to identify
candidate Questions while preserving explicit resolution authority.

## Persistence and conformance

Relations are persisted in EventJournal and restored on replay. Tests cover
relation types, endpoint validation, multi-edge dependency-cycle rejection,
atomic rollback, and restart persistence.
