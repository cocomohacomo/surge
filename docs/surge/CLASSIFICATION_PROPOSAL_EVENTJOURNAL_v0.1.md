# Durable ClassificationProposal EventJournal Contract v0.1

## Goal

Persist the exact schema-validated, non-committing ClassificationProposal
produced by the recognition adapter in the same durable transaction as its
RecognitionPacket admission and SemanticDelta. The proposal remains audit
metadata, not an instruction to mutate the ContextGraph.

## Transaction boundary

RecognitionPacket + SemanticDelta + CLASSIFICATION_PROPOSAL_RECORDED are
committed under one EventJournal COMMIT marker. Failed transactions expose
none of these records; a successful transaction advances state_version once.

## Event record

- Kind: `CLASSIFICATION_PROPOSAL_RECORDED`
- ID: `classification-proposal:<proposal_id>`
- Payload: versioned P1 record containing proposal ID, packet reference,
  base and ContextHint versions, packet source generation/sequence, and exact
  serialized proposal JSON.
- Core enforces lineage equality, non-empty identifiers and a 1 MiB JSON cap.
- The proposal is audit metadata only. It cannot mutate ContextGraph state.

## Replay and retrieval

EventJournal is the persistence authority. ContextGraph replay ignores the
proposal audit event as graph metadata. The C API retrieves the exact JSON by
proposal ID; Python decodes it and verifies the embedded ID. Retrieval works
after reopening the same journal.

## Compatibility and trust

The existing `surge_packet_core_admit_and_apply` ABI remains unchanged and
delegates to the extended API with no proposal. Schema validation remains an
adapter responsibility. Core does not reinterpret model labels or grant
proposal commit authority. Existing journal framing and recovery are reused.

## Conformance

Tests cover atomic packet/semantic/proposal commit, exact restart retrieval,
stale lineage rejection without partial commit, and legacy ABI compatibility.
