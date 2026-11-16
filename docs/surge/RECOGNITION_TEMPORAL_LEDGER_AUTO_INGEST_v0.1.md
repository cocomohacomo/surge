# RecognitionPacket to Temporal Ledger Automatic Ingestion v0.1

## Eligibility

Automatic ingestion is permitted only after packet and SemanticDelta admission
succeed and the conservative binder returns `BOUND_PENDING_CORE_REVIEW`.
The candidate must be `RESOLVED`, not a correction, and have one supported
role: `VALID_FROM`, `VALID_UNTIL`, or `INTERVAL`. Claim, Evidence, and
packet IDs are mandatory and preserved as provenance.

## Idempotency and ordering

Transaction ID, idempotency key, and event ID are deterministic from packet ID
and candidate role. The runtime reads the current EventJournal state version
immediately before the write. Exact retries use the same identifiers.

Packet/SemanticDelta commit and temporal-ledger contribution are separate
transactions. A process failure between them may leave a committed packet
without its temporal proposal. Reprocessing the same packet can safely
complete the second transaction without creating a duplicate contribution.

## Safety

Unresolved cues, missing timezone/year/reference, corrections, multiple cues,
multi-clause ambiguity, invalid source spans, and unaccepted Claims are not
written. Ledger records remain review proposals only. No Evidence entailment
verification, temporal approval, or ClaimValidity mutation occurs here.

## Retrieval and review

Contributions are retrievable through the existing Claim-keyed ledger read API.
A complete boundary pair remains pending until explicit review or disposition.
The approved ClaimValidity Core API remains the only authority for validity
revisions.
