# Claim Temporal Validity v0.1

Status: Core implementation contract

## 1. Purpose

Claim lifecycle answers whether an assertion is active, superseded or retracted in the graph. Temporal validity answers when the assertion applies in the modeled world. These are independent dimensions: an ACTIVE Claim can describe a future or expired interval, and a SUPERSEDED Claim can remain historically valid for an earlier period.

SURGE therefore stores valid time separately from transaction time.

## 2. Valid-time interval

Each validity revision defines a half-open interval:

`[valid_from, valid_until)`

- `valid_from` is required.
- `valid_until` is optional. Empty means open-ended.
- Both values use RFC3339 date-time strings with an explicit timezone.
- Core compares instants after timezone-offset normalization.
- `valid_from` MUST be earlier than `valid_until` when an end exists.
- The end instant is exclusive.

A timestamp without an explicit timezone is rejected. Invalid calendar dates, invalid offsets and malformed timestamps are rejected. No local timezone or UTC default is inferred.

## 3. Evidence and revisions

Every validity revision MUST reference an existing Evidence node that supports the temporal boundary. The referenced Claim and Evidence must already exist in the graph or be created earlier in the same SemanticDelta.

A revision number starts at 1 and increases by exactly one per Claim. Revisions are append-only. A corrected date range creates a new revision; it does not rewrite the prior range. The latest revision is the current temporal assertion, while all prior revisions remain available for audit.

The record stores `recorded_state_version`, distinct from the valid-time interval. This provides transaction-time lineage: when SURGE learned or accepted the validity assertion.

## 4. Temporal evaluation

For an instant `t`, Core returns:

- `UNKNOWN`: no validity assertion exists, or the query instant is invalid.
- `NOT_YET_VALID`: `t < valid_from`.
- `VALID`: `valid_from <= t < valid_until`, or no end is specified.
- `EXPIRED`: `t >= valid_until`.

Unknown is not treated as valid. Claim lifecycle state does not override temporal evaluation; consumers combine the two dimensions according to their task.

## 5. Atomicity and replay

Validity revisions are included in SemanticDelta fingerprinting and journal encoding. The Claim, basis Evidence, validity revision and any other Delta operations commit atomically. Validation failure leaves state version and Journal unchanged.

Journal replay validates the same timestamp, Evidence and revision constraints and reconstructs the full revision history. Exact idempotent retries return the original commit result.

## 6. Retrieval contract

Claim retrieval includes the latest `claim_validity` object, or null when no validity assertion exists. The object contains `revision`, `valid_from`, `valid_until`, `basis_evidence_id` and `recorded_state_version`. Historical revisions remain available from the Core history API.

## 7. Error behavior

- `CLAIM_VALIDITY_CLAIM_INVALID`: target is not an existing Claim.
- `CLAIM_VALIDITY_EVIDENCE_INVALID`: basis is not an existing Evidence.
- `CLAIM_VALIDITY_TIMESTAMP_INVALID`: malformed or timezone-free RFC3339 timestamp.
- `CLAIM_VALIDITY_INTERVAL_INVALID`: end is not later than start.
- `CLAIM_VALIDITY_REVISION_INVALID`: revision sequence is not contiguous.

## 8. Scope boundary

This version implements explicit valid-time intervals, timezone-normalized comparison, append-only interval revisions, transaction-version lineage and point-in-time evaluation. It does not yet implement automatic derivation of intervals from natural-language dates, temporal dependency propagation, interval overlap resolution across separate Claims, or a bitemporal query API across arbitrary historical transaction versions.
