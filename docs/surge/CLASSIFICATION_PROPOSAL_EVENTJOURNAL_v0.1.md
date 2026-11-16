# Durable ClassificationProposal EventJournal Contract v0.1

## Goal

Persist the exact schema-validated, non-committing ClassificationProposal
produced by the recognition adapter in the same durable transaction as its
RecognitionPacket admission and SemanticDelta. The proposal remains audit
metadata, not an instruction to mutate the ContextGraph.

## Transaction boundary

The atomic write order is:

```text
RecognitionPacket
  + DELTA_META and SemanticDelta events
  + CLASSIFICATION_PROPOSAL_RECORDED
  -> one EventJournal COMMIT marker
```

The proposal event is appended to the existing semantic event batch before
PacketAdmissionCoordinator calls EventJournal::commit. A failed transaction
therefore exposes neither the packet, semantic graph changes, nor proposal.
A successful transaction advances EventJournal state_version exactly once.

## Event record

- Event kind: `CLASSIFICATION_PROPOSAL_RECORDED`
- Event ID: `classification-proposal:<proposal_id>`
- Payload encoding: versioned `P1` record containing proposal ID, packet
  reference, base state version, ContextHint state version, packet source
  generation, packet source sequence, and the exact serialized proposal JSON.
- The JSON payload is produced from the schema-validated Python proposal.
  Core does not reinterpret model labels or grant the proposal commit authority.
- Core enforces non-empty identifiers, 1 MiB serialized JSON maximum, packet
  reference equality, source generation/sequence equality, and equality of
  proposal base/context-hint versions with the transaction's expected version.

The event ID is globally unique under EventJournal's existing event-ID rule.
A conflicting ID rejects the entire transaction.

## Replay and retrieval

EventJournal's committed event stream is the source of truth. Packet admission
recovery continues to rebuild packet sequencing from packet events.
ContextGraph replay deliberately ignores proposal audit events while replaying
typed graph state; they do not create graph nodes or increment state version
independently. The enclosing semantic packet transaction still increments the
version once.

The C API `surge_classification_proposal_get` scans committed events by
proposal ID and returns the exact serialized JSON. Python
`SemanticCoreBridge.get_classification_proposal` decodes that JSON and checks
that its embedded proposal ID matches the requested ID. The operation works
after closing and reopening the Core against the same journal.

## Trust and limits

- Schema validation remains an adapter responsibility. Core performs lineage
  and size checks but does not parse arbitrary model-supplied JSON.
- The proposal is immutable audit evidence. It cannot alter Claims, relations,
  Focus, or any other ContextGraph state.
- No separate proposal-only transaction is permitted for this integration.
- Existing journal framing, CRC, commit marker, and recovery semantics are
  reused without changing the journal file format.
- EventJournal's existing maximum frame size remains the final upper bound;
  the proposal JSON limit is deliberately lower at 1 MiB.
- A proposal lookup is by exact proposal ID. Enumeration/pagination and
  retention/compaction policy are outside this increment.

## Conformance cases

The C++ journal test covers:
1. Packet, SemanticDelta, and proposal committed in one transaction.
2. Proposal retrieval after a fresh Core open.
3. Exact serialized JSON round-trip.
4. Stale proposal lineage rejection without a partial commit.

The Python LLM integration test covers passing the schema-validated proposal
through the atomic adapter path alongside SemanticDelta.
