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


## Aggregate Question state API v0.1

A Question aggregate is derived at read time from every durable QuestionAspect
whose references include that Question ID. It is not stored separately, so
aspect updates cannot leave a stale denormalized Question status.

Precedence:
1. Any OPEN aspect makes the Question OPEN.
2. Otherwise, any DEFERRED aspect makes the Question DEFERRED.
3. If one or more aspects exist and all are RESOLVED, the Question is RESOLVED.
4. A valid Question with no aspects is UNKNOWN. A missing or non-Question ID
   returns QUESTION_NOT_FOUND.

The read returns status, counts for OPEN/DEFERRED/RESOLVED, sorted aspect_ids,
and the EventJournal state_version observed by the read. The getter is
read-only and does not advance the version.

- C ABI: surge_question_get_aggregate_status
- Python: SemanticCoreBridge.get_question_aggregate(question_id)

The version identifies the graph snapshot represented by the result.
ContextGraph does not currently promise concurrent multi-threaded mutation
safety; callers that share a handle across threads must serialize access.
