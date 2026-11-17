# Durable Cross-Packet Temporal Ledger v0.1

## Purpose

Persist complementary temporal-boundary proposals across packets using the
existing append-only EventJournal. The ledger is reconstructed by replaying
committed typed events, so pending state survives process restart.

## Event contract

- `TEMPORAL_BOUNDARY_CONTRIBUTION_V1`: Claim ID, role, boundary value(s),
  Evidence ID and source packet ID.
- `TEMPORAL_BOUNDARY_CLEARED_V1`: explicit Claim-scoped disposition/clear.
- Each event is committed as a journal transaction with the caller's
  transaction ID, idempotency key, event ID and expected state version.
- Journal transaction atomicity, event-ID uniqueness, state-version checks,
  idempotency and incomplete-tail recovery remain provided by EventJournal.
- Replay processes committed events in journal order. Unknown event kinds are
  ignored by this domain ledger; malformed recognized events fail replay closed.

## State and safety

- Keying is strictly by caller-supplied Claim ID. No Claim identity inference.
- Every contribution requires Evidence ID and packet ID.
- The default bound is 4096 pending Claims. Capacity overflow rejects before
  journal append; no silent eviction.
- Conflicting same-role boundaries and invalid lexically ordered ISO-like
  intervals are rejected for review. The lexical guard is conservative and is
  not a substitute for Core's timezone-aware temporal validation.
- A complete pair is reported as `BOUNDARIES_COMPLETE_PENDING_REVIEW`, not
  as a validated or commit-ready ClaimValidity interval.
- The ledger never writes ClaimValidity. Existing Core review, Evidence
  validation, revision checks and dependency reevaluation remain mandatory.
- Clear/disposition is itself an append-only journal event.

## Integration boundary

The component accepts an EventJournal reference so an owning transaction
coordinator can share the same journal instance. It is a reusable C++ Core
component, but is not yet wired into the Python bridge or automatic
RecognitionPacket ingestion path. A caller must serialize journal mutations
through the same coordinator and pass the current expected state version.
EventJournal's existing single-process writer limitation remains in force.

## Durability limits

Replay restores committed events only. A partial transaction is discarded by
EventJournal recovery. The journal's existing CRC32 and flush semantics apply;
this component does not claim fsync-level power-loss durability or cryptographic
tamper evidence.


## Selection watermark and later conflict reopening

Candidate selection is bound to the latest candidate ID for the selected role
at the time of the selection event. The watermark is persisted in selection
event version 2; replay remains compatible with version 1 by deriving the
watermark from candidates present at that historical point. Candidates added
after the watermark are evaluated against the selected interval. A later
conflicting candidate reopens review, while conflicts already present at the
time of selection remain resolved by that explicit selection.
