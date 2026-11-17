# Core Temporal Reassessment Integration v0.1

## API

`ContextGraph::reassess_claim(claim_id, reference_at, expected_state_version, out, error)`
performs a read-only evaluation against the current Core snapshot, then appends
the resulting immutable record as one EventJournal transaction. The caller
must supply an explicit RFC3339 reference instant and the exact expected Core
state version. A mismatch returns `STATE_VERSION_CONFLICT`; no trace is written.

## Captured decision grounds

Each record stores the Claim lifecycle, selected ClaimValidity revision and
transaction version, evaluated interval and temporal state. It stores every
Evidence relation to the Claim for SUPPORTS, CONTRADICTS and RETRACTS, including
the relation ID, selected EvidenceValidity revision and transaction version,
valid-time interval, point-in-time state and eligibility. The active support
and contradiction sets are stored separately.

Evidence without explicit valid-time metadata remains UNKNOWN and is never
counted as active support or contradiction. Expired and not-yet-valid Evidence
remains in the trace but does not affect the current decision. RETRACTS is
preserved as a distinct relation and is not conflated with contradiction.
An inactive Claim or a Claim not VALID at the reference instant cannot be
reported as currently supported.

Decision codes: CLAIM_INACTIVE, CLAIM_NOT_CURRENT, TEMPORAL_CONFLICT,
CONTRADICTED, SUPPORTED, INSUFFICIENT_CURRENT_EVIDENCE.

## Immutable chain and transaction time

Each record captures evaluated_state_version and recorded_state_version
separately. The latter is the EventJournal commit version. Per-Claim sequence,
previous hash, canonical snapshot SHA-256 and chain hash make the trace
append-only and tamper-evident. A new evaluation creates a new record; it never
rewrites a prior decision.

The journal commit uses the expected state version as compare-and-swap.
Replay verifies sequence, previous hash, snapshot digest, chain hash, the
referenced graph relations, the Claim revision and Evidence revisions against
the exact graph state preceding the trace event. A trace that does not match
that state fails recovery rather than being silently accepted.

## Authority and boundaries

This API is Core-owned and read-only with respect to semantic facts. It does
not create, revise or retract Claims or Evidence. It does not automatically
promote RETRACTS into Claim lifecycle changes. Such lifecycle changes still
require a separate validated SemanticDelta. Provenance timestamps
(observed/published/retrieved) are not inferred from content and remain a
future explicit Evidence metadata extension.
