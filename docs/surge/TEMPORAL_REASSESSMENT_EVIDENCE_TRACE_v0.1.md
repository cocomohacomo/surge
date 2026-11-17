# Temporal Reassessment and Evidence Validity Trace v0.1

## Purpose

A reassessment must explain more than which Claim and Evidence IDs were visited.
It must preserve the temporal grounds on which each item was considered usable
at a particular reference instant. This adapter complements the existing
append-only ClaimValidity revisions; it does not replace Core's authority.

## Separate time axes

- **Claim valid time**: the half-open interval `[valid_from, valid_until)`
  from the selected ClaimValidity revision.
- **Evidence valid time**: the half-open interval during which the Evidence
  supports, contradicts, or retracts the Claim in the modeled world.
- **Evidence provenance time**: `observed_at`, `source_published_at`, and
  `source_retrieved_at`. These describe when the evidence was observed or
  acquired, not when its assertion is true.
- **Transaction time**: `recorded_state_version` and the Claim revision identify
  when SURGE recorded the information.
- **Reassessment time**: the caller-supplied, timezone-aware `reference_at`.

No clock, timezone, or missing validity bound is inferred. Missing
`valid_from` yields UNKNOWN, not VALID. Intervals are half-open, and an
exclusive `valid_until` instant is already expired.

## Trace contract

`evaluate_claim_reassessment(claim, evidence, reference_at=...)` produces an
immutable explanation snapshot containing:

- Claim ID, Claim validity revision, lifecycle, transaction version and the
  evaluated interval/status/reason.
- Every Evidence ID, relation type, evaluated validity interval/status/reason,
  observation/publication/retrieval timestamps, transaction version and
  eligibility at the reference instant.
- The resulting decision and stable reason codes, plus the exact Evidence IDs
  counted as active support or active contradiction.
- A canonical SHA-256 snapshot digest and content-addressed trace ID.

Evidence relations are SUPPORTS, CONTRADICTS and RETRACTS. A currently valid
support and a currently valid contradiction produce TEMPORAL_CONFLICT.
Expired or not-yet-valid Evidence is retained in the trace but does not count
as current support. An Evidence item with no explicit valid_from remains
UNKNOWN and cannot silently support a current Claim.

## Append-only trace ledger

`TemporalReassessmentLedger` appends immutable snapshots with sequence,
previous hash and chain hash. `replay` verifies sequence continuity, chain
links, content hashes and the original snapshot digest. Exported records can be
persisted by the owning Core/EventJournal transaction coordinator. This
adapter's local ledger is not itself a durable disk store and does not claim
Core-level transaction atomicity.

## Authority boundary

This component is read-only. It does not create or revise Claims, Evidence,
ClaimValidity, or relations. The caller must supply authoritative Claim and
Evidence records retrieved from Core. Natural-language temporal recognition
remains an untrusted proposal source. A future Core integration should persist
the same trace record in the EventJournal and revalidate all referenced
revisions and Evidence IDs under the same expected state version.

## Conformance coverage

Tests cover half-open boundaries, missing validity, expired Evidence,
contradictory Evidence, inactive Claims, timezone and interval errors,
append-only sequence, replay, tamper detection, and the read-only boundary.
