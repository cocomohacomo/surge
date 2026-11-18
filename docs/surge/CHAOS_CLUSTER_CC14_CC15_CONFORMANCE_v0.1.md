# Chaos Cluster CC14 / CC15 Conformance v0.1

Status: executable regression contract

## CC14: PatternHypothesis is not a Claim

A `PatternHypothesis` is an exploratory interpretation owned by a Chaos
Cluster. It is not a graph `Claim`, even when its review status reaches
`PROMOTED_TO_EVIDENCE_REVIEW`.

Required invariants:

- Adding or reviewing a PatternHypothesis MUST NOT create a Claim node.
- The hypothesis ID MUST NOT be treated as a Claim ID by graph retrieval.
- Review status, epistemic state, confidence, and exclusion rationale remain
  hypothesis-local metadata.
- `PROMOTED_TO_EVIDENCE_REVIEW` means only that explicit evidence review may
  be conducted. It does not assert that evidence exists, satisfy a Claim's
  evidence requirements, or authorize Claim creation.
- Claim creation remains an explicit SemanticDelta operation with its own
  evidence references, provenance, scope, and validation.

## CC15: Question resolution is independent of Cluster lifecycle

A Question and its QuestionAspects belong to the contradiction/interest
resolution process. Resolving an aspect changes only that aspect's status and
the derived Question aggregate.

Required invariants:

- Resolving or deferring a QuestionAspect MUST NOT promote, archive, close,
  dormancy-transition, or otherwise mutate a Chaos Cluster.
- A Question aggregate becoming RESOLVED MUST NOT imply that its related
  Cluster is resolved or complete.
- Cluster lifecycle changes remain explicit, provenance-bearing
  ChaosClusterLedger transactions.
- Both state machines remain replayable from their shared EventJournal without
  replaying one domain's event as a mutation in the other domain.

## Executable coverage

The C++ QuestionRouter conformance test runs ContextGraph and
ChaosClusterLedger over one EventJournal. It:

1. creates an evidence-review PatternHypothesis and checks no Claim node is
   created;
2. routes a contradiction into a Question and resolves its QuestionAspect;
3. checks the Chaos Cluster lifecycle, lifecycle history, hypothesis status,
   and promotion map remain unchanged;
4. reopens the journal and verifies both domains replay independently.

This contract does not claim that every CC01-CC20 scenario is closed. The
remaining matrix must include independent negative, stale-version,
idempotency, migration, and restart cases for each operation.
