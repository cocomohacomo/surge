# Atomic Packet and Semantic Transaction v0.1

## Contract

A text packet and its first SemanticDelta are one durable unit. The Core must
not leave an admitted RecognitionPacket without its corresponding semantic
projection when the semantic proposal is invalid or the journal commit fails.

The C++ sequence is:
1. Validate the packet snapshot, UTF-8, NFC byte range and SHA-256 digests.
2. Build a typed SemanticDelta.
3. ContextGraph.prepare_events validates the expected state version, graph
   references, relations and ScopeSpan proposals without mutating graph state.
4. PacketAdmissionCoordinator stages sequencing/range state.
5. EventJournal commits one transaction containing a SEMANTIC_PACKET_ADMITTED
   record followed by DELTA_META and all graph events.
6. Only after the commit marker succeeds does PacketAdmissionCoordinator publish
   packet state. ContextGraph replays the committed transaction to publish graph
   state.

The journal transaction uses the packet idempotency key as its transaction key.
The semantic idempotency key and fingerprint are retained in DELTA_META and
rebuilt by ContextGraph during replay.

## Replay and versioning

Legacy RECOGNITION_PACKET_ADMITTED events remain supported and count as one
state-version transition. SEMANTIC_PACKET_ADMITTED is the packet half of a
combined transaction and does not independently advance the version; the
following DELTA_META and graph events form the same single transition.

The semantic delta's expected version is extracted from DELTA_META and checked
against the journal's current version. EventJournal performs the final
compare-and-commit under its transaction lock, so a concurrent intervening
commit is rejected rather than allowing a stale semantic projection.

## Retry behavior

- A successful exact retry resolves through packet and semantic idempotency
  indexes and does not append another transaction.
- Reusing a packet or semantic idempotency key with different content is
  rejected.
- A packet already admitted through the legacy packet-only path cannot silently
  be upgraded by a second non-empty semantic transaction under the same packet
  identity; it returns PACKET_ALREADY_ADMITTED_SEMANTIC_NOT_COMMITTED and needs
  explicit recovery handling.
- Semantic validation failure happens before packet commit and leaves both
  packet count and journal version unchanged.

## Interfaces and tests

- C++: ContextGraph.prepare_events and
  PacketAdmissionCoordinator.admit(packet, snapshot, semantic_events)
- C ABI: surge_packet_core_admit_and_apply
- Python: RecognitionPacketAdapter.admit_and_apply_to_core and
  SemanticCoreBridge.admit_and_apply
- LLM runtime: RecognitionPacket -> structured LLM candidate -> scoring ->
  typed SemanticDelta -> single Core transaction

The atomic conformance test covers commit, exact retry, restart replay, and
pre-commit semantic rejection. The end-to-end LLM tests now expect one journal
state-version increment per input packet, rather than separate packet and
semantic increments.

## Scope boundary

This API currently combines packet admission with node/relation/focus
SemanticDelta operations used by the LLM runtime. ScopeSpan is already a
first-class persisted ContextGraph record, but the combined C ABI does not yet
carry ScopeSpan arrays. A scope-bearing atomic API extension is required before
LLM-generated ScopeSpan proposals can use this one-transaction path.


## Fault-injection and crash-boundary conformance

EventJournal provides an optional AppendFrameGate constructor overload for
deterministic storage-failure tests. The gate receives the zero-based count of
successfully appended frames and can reject the next frame. The default
constructor has no gate and preserves normal production behavior.

The journal test suite injects failure at each frame boundary of a two-event
transaction:
1. before BEGIN;
2. after BEGIN, before the first EVENT;
3. after the first EVENT, before the second EVENT;
4. after both EVENT frames, before COMMIT.

For each failure point, the test verifies that the in-memory committed version
does not advance, reopening discards the incomplete transaction, previously
committed events remain intact, and retrying the same transaction succeeds
exactly once. This tests the transaction visibility boundary rather than
assuming that a successful write of BEGIN or EVENT frames is a commit.

