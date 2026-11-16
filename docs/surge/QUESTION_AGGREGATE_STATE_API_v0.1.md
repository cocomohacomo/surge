# Question Aggregate State API v0.1

## Purpose

Expose one deterministic Question-level state derived from durable
QuestionAspect lifecycle records.

## Aggregate rules

- Any OPEN aspect => OPEN.
- Otherwise, any DEFERRED aspect => DEFERRED.
- One or more aspects and all RESOLVED => RESOLVED.
- A valid Question with no aspects => UNKNOWN.
- Missing IDs and IDs that are not Question nodes => QUESTION_NOT_FOUND.

The order above is the precedence order. Counts and sorted aspect IDs are
returned alongside the aggregate status.

## Consistency and authority

The aggregate is computed on read from the authoritative graph and persisted
QuestionAspect status map. No aggregate status is denormalized or persisted.
The getter is read-only and does not change EventJournal state. The returned
state_version identifies the version observed by the read.

ContextGraph does not currently guarantee concurrent mutation safety across
threads. Callers sharing a Core handle must serialize access. This API does
not resolve aspects or change ClaimValidity, ClaimStatus, evidence ranking,
Focus, or external systems.

## APIs

- C ABI: surge_question_get_aggregate_status
- Python: SemanticCoreBridge.get_question_aggregate(question_id)

The JSON response contains question_id, status, counts (OPEN, DEFERRED,
RESOLVED), sorted aspect_ids, and state_version.

## Verification

Conformance tests cover empty Questions, missing IDs, OPEN precedence,
DEFERRED precedence, all-resolved state, exact counts, sorted IDs,
read-only state-version behavior, and restart/replay consistency.
