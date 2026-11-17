# Compact Recognition Pipeline v0.1

## Pipeline

```text
Source text
  -> CompactRecognitionRuntime
  -> NFC-bound RecognitionExecution
  -> RecognitionPacket + proposal sidecar
  -> RecognitionCandidateScoringBridge
  -> ACCEPT / REVIEW / ABSTAIN partitions
  -> explicit PacketAdmissionPort call
```

`CompactRecognitionPipeline.prepare` is read-only. It coordinates model
inference, source packet construction and candidate scoring, then returns a
`PreparedRecognition` object. It does not call the Core, create SemanticDelta,
or persist Claims/Evidence.

`admit_packet` is a separate explicit operation. It admits only the
source-centric RecognitionPacket through the existing adapter/Core boundary.
It does not commit candidate semantics. A later semantic-application stage
must independently validate candidate scope, references, dependencies,
contradictions, retractions, temporal validity and State Version, then submit
an atomic Core transaction.

## Invariants

- Packet and sidecar share one normalized source SHA-256 and packet ID.
- Candidate offsets are UTF-8 byte offsets in NFC source coordinates.
- ACCEPT, REVIEW and ABSTAIN remain distinct; none is commit authorization.
- Packet admission is explicit and separated from preparation.
- The source snapshot passed to Core admission is retained verbatim; the
  adapter normalizes and verifies it against the packet.
- Performance certification includes inference, packet construction,
  scoring, Core admission and any semantic commit path. Model-only RTF is
  insufficient for the deployment target.

## Extension points

The inference backend, relation scorer, packet adapter and Core bridge are
independent ports. They can be replaced without changing the orchestration
contract. A future native pipeline may move hot paths into C++ while
preserving packet and candidate identity semantics.
