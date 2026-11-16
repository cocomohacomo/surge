# Temporal Question Core Integration v0.1

## Automatic synchronization

The C API handle now owns both TemporalBoundaryLedger and
TemporalQuestionLedger over the same EventJournal. After a successful
boundary contribution or explicit candidate selection, the C API synchronizes
the resulting pending snapshot into the question ledger.

The synchronization uses deterministic transaction, idempotency, and event
identifiers derived from the originating boundary event. Repeating the C API
operation therefore retries the same question snapshot rather than creating a
second Question. Multi-role snapshots remain one EventJournal event.

The existing Python `SemanticCoreBridge.add_temporal_boundary` and
`resolve_temporal_boundary_candidate` methods automatically benefit from this
path. New read APIs are:

- `get_temporal_questions(claim_id)`: returns durable Question snapshots
  associated with a Claim.
- `temporal_question_count`: returns the number of durable temporal Questions.

## Lifecycle and authority

A conflicting set opens a Question. Further conflicting candidates update the
same deterministic Claim/role Question. Explicit candidate selection moves it
to `RESOLVED_PENDING_APPROVAL`; a later conflicting contribution reopens it.
The API exposes candidate IDs and their Evidence/Packet provenance.

Question synchronization is a separate append-only journal transaction after
the boundary event. If synchronization fails after the boundary commit, the
caller receives an error and can safely retry the same boundary operation
using the same transaction/idempotency/event identifiers. The question event
is idempotent. Consumers must use the returned global state version because
question snapshots also advance the shared EventJournal version.

This layer does not mutate ClaimValidity, execute actions, or assert that
Evidence semantically entails a candidate. It records a review question and
routes its provenance for further reasoning.

## RecognitionPacket path

The existing conservative `persist_temporal_binding` adapter path calls
`add_temporal_boundary` for only resolved, accepted, proposal-only temporal
bindings. Because synchronization now occurs inside that Core API path,
eligible RecognitionPacket temporal proposals automatically create or update
the associated durable Question whenever a conflict exists. Ambiguous,
unresolved, multi-cue, correction, or unbound proposals remain review-only
and are not promoted into the ledger.
