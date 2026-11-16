# Temporal Boundary Ledger C API and Python Bridge v0.1

## API

The Core handle owns a \`TemporalBoundaryLedger\` sharing its existing
EventJournal. Opening the handle replays committed temporal ledger events.
The C ABI exposes add, get, clear, and pending-count operations. The Python
\`SemanticCoreBridge\` exposes \`add_temporal_boundary\`,
\`get_temporal_boundary\`, and \`clear_temporal_boundary\`.

## Transaction contract

Every write requires caller-supplied transaction ID, idempotency key, event ID,
and expected EventJournal state version. The ledger shares the Core journal,
so callers must serialize writes with other journal writers and use the
current state version. Exact retries use the same idempotency key.

## Safety boundary

This bridge persists proposals only. It does not infer Claim identity, validate
Evidence entailment, approve intervals, or mutate ClaimValidity. A complete
pair remains pending review. Automatic RecognitionPacket extraction and
retrieval indexing remain separate integration tasks.


## RecognitionPacket automatic ingestion v0.1

After packet and SemanticDelta admission succeed, the LLM runtime may persist a
temporal binding only when the conservative binder returns
`BOUND_PENDING_CORE_REVIEW`. The candidate must be `RESOLVED`, not a
correction, and use one supported role. Claim, Evidence, and packet IDs are
carried as provenance.

Transaction, idempotency, and event IDs are deterministic from packet ID and
candidate role. The runtime reads the current Core state version immediately
before the ledger write. Exact retries reuse the same identifiers.

Unresolved timezone/year/reference cues, corrections, multiple cues,
multi-clause scope, invalid source spans, and unaccepted Claims are not
persisted. Packet admission and temporal contribution are separate
transactions. A crash between them may leave a packet without a temporal
proposal; reprocessing the same packet can safely complete the second write.

This path persists review proposals only. It does not verify Evidence
entailment, approve temporal meaning, or mutate ClaimValidity.


## Conflict preservation and candidate review v0.1

Every structurally valid contribution is appended even when its boundary
conflicts with an earlier contribution. The journal event ID is exposed as
the candidate ID. Each candidate retains its role, boundary values, Evidence
ID, and Packet ID. Conflicts report `BOUNDARY_CONFLICT_REVIEW_REQUIRED`;
no latest-wins overwrite occurs.

The read API returns every candidate and its selection state. Reviewers can
select a candidate with `resolve_temporal_boundary_candidate`, which appends
a durable disposition event. This selects a proposal for subsequent review
only. Its state is `CONFLICT_RESOLVED_PENDING_APPROVAL`; it does not approve
the temporal interpretation or change ClaimValidity. Candidates and
dispositions survive restart and replay.
