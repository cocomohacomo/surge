# Shared QuestionAspect Lifecycle API v0.1

## Purpose

Provide one status API for QuestionAspects regardless of whether the Question
originated from a temporal boundary conflict or an ordinary Claim contradiction.

## State model

- OPEN: unresolved or reopened attention
- DEFERRED: explicitly postponed, not resolved
- RESOLVED: explicitly marked resolved by a caller

Transitions are explicit. A later review may reopen a resolved or deferred
aspect by setting OPEN. This is a workflow state only: it does not mutate
ClaimValidity, ClaimStatus, evidence ranking, or external systems.

## API

- C ABI: surge_question_aspect_get_status
- C ABI: surge_question_aspect_set_status
- Python: SemanticCoreBridge.get_question_aspect
- Python: SemanticCoreBridge.set_question_aspect_status

Updates use the EventJournal state version as an optimistic concurrency token.
A stale expected version is rejected. Status updates are durable and replayed
with the graph. The getter returns the current status and state version.

## Boundaries

The QuestionAspect is the common lifecycle unit. TemporalQuestionLedger
continues to retain temporal candidate provenance and its richer
RESOLVED_PENDING_APPROVAL / REOPENED statuses. This API does not collapse
that specialized evidence ledger into the workflow status. In particular,
RESOLVED here means the review aspect is closed, not that a candidate has
been approved into ClaimValidity.

## Verification

Tests cover OPEN → RESOLVED → DEFERRED → OPEN, state-version control,
and restart/replay persistence for a general contradiction Question.


## Cross-origin conformance

The same QuestionAspect lifecycle API applies to Questions created by the
temporal boundary workflow and by ordinary Claim contradictions. The workflow
status is intentionally separate from the temporal candidate ledger status.
A temporal candidate may remain `RESOLVED_PENDING_APPROVAL` while its
QuestionAspect is explicitly reopened for further review. Changing the
QuestionAspect never approves or changes ClaimValidity.

Conformance coverage exercises OPEN → RESOLVED → DEFERRED → OPEN for both
origins, stale state-version rejection, and restart/replay persistence.
