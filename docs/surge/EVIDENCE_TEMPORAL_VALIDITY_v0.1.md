# Core Evidence Temporal Validity v0.1

## Purpose

Claim validity and Evidence validity are separate temporal dimensions.
Evidence may describe a source, observation, record, or assertion whose
relevance in the modeled world is bounded independently of the Claim it
supports. Provenance timestamps such as observed, published, and retrieved
time are not substitutes for valid time.

## Record

Each append-only EvidenceValidity revision contains:

- evidence_id: the Evidence whose valid-time interval is asserted.
- valid_from and optional valid_until: RFC3339 timestamps with explicit
  timezone, defining the half-open interval [valid_from, valid_until).
- basis_evidence_id: an existing Evidence that supports the boundary.
- revision: contiguous per subject Evidence, starting at 1.
- Optional review_evidence_id and reviewer_id, which must be supplied
  together.
- recorded_state_version: the transaction version that committed the
  revision.

The basis may be the subject Evidence itself, or a distinct Evidence connected
to it by an explicit SUPPORTS relation from basis to subject. Review provenance
must reference an existing Evidence node.

## Evaluation

ContextGraph::evidence_validity_at(evidence_id, instant) returns the same
states as Claim temporal evaluation:

- UNKNOWN when no validity revision exists or the instant is invalid.
- NOT_YET_VALID before valid_from.
- VALID when valid_from <= instant < valid_until, or valid_until is open.
- EXPIRED at or after valid_until.

No timezone, clock, or missing boundary is inferred. The latest committed
revision is used for current-state queries; the full revision history remains
available for audit.

## Atomicity and recovery

Evidence validity updates are included in SemanticDelta request fingerprinting
and are validated against the staged graph. They commit in the same EventJournal
transaction as the subject Evidence, basis Evidence, supporting relation, and
any Claim changes in that Delta. A failed update leaves graph state and journal
version unchanged. Journal replay revalidates each revision and restores the
full history after restart.

## Authority boundary

Recognition adapters may propose Evidence validity candidates but cannot write
them directly. Only Core accepts a validated EvidenceValidity update through
SemanticDelta. The update does not itself change Claim lifecycle or rewrite
Claim validity.
