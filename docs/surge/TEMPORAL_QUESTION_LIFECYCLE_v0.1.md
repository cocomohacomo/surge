# Temporal Question Lifecycle v0.1

## Purpose
Represent conflicting temporal-boundary candidates as a durable, reviewable
Question. The ledger is derived from TemporalBoundaryPending and shares its
EventJournal.

## Identity and links
A question has deterministic identity:
question/temporal/{claim_id}/{role}
Each snapshot preserves Claim ID, temporal role, all candidate IDs, Evidence
IDs, and Packet IDs for that role. Repeated synchronization with the same
EventJournal idempotency key is safe. New candidates update the same question.

## Lifecycle
- OPEN: first conflicting candidate set.
- UNDER_REVIEW: conflict remains while the question is active.
- RESOLVED_PENDING_APPROVAL: an explicit candidate disposition exists.
- REOPENED: a new conflicting candidate arrives after a disposition.

Resolution is a review proposal, not temporal approval, and MUST NOT update
ClaimValidity. The caller synchronizes after each relevant boundary-ledger
event and supplies the current EventJournal state version plus unique
transaction, idempotency key, and event ID.

## Persistence and safety
Snapshots are append-only TEMPORAL_QUESTION_SNAPSHOT_V1 journal events.
Replay restores the latest snapshot for each deterministic question ID.
The ledger defaults to 16,384 questions. It makes no machine-clock inference,
does not choose candidates automatically, does not mutate ClaimValidity or
dependency state, and records provenance references rather than proof of
Evidence entailment.

This first implementation is a durable C++ Core ledger. ContextGraph retrieval,
C ABI/Python bridge exposure, and automatic synchronization from packet
ingestion remain follow-on integration work.
