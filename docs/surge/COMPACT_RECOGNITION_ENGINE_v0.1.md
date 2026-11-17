# Compact Recognition Engine Interface v0.1

## Purpose

This interface lets SURGE use a small local recognition model without binding
the Core to a specific inference runtime. The initial deployment target is a
model artifact no larger than 30 MiB and CPU execution. The performance target
is end-to-end RTF < 0.01 when the caller supplies the source audio duration.

These are acceptance targets, not measured claims. A concrete model/backend
must be benchmarked on the target hardware before it can be marked compliant.

## Boundary

- `CompactModelSpec.inspect` verifies that the artifact exists, is non-empty,
  is within the configured byte limit, and records its SHA-256 digest.
- `CompactModelBackend` is the replaceable backend protocol. ONNX Runtime,
  a native C/C++ runtime, or another local implementation can satisfy it.
- `CompactRecognitionRuntime` validates input and typed candidates, measures
  wall-clock inference time, and optionally computes RTF.
- The runtime returns recognition candidates only. It has no Core handle,
  journal access, or SemanticDelta commit method.
- RecognitionScorer, proposal builders, packet admission and Core validation
  remain separate stages. Candidate confidence does not grant commit authority.

## Contract

The backend implements:

```python
recognize(text: str, context: dict | None) -> Sequence[RecognitionCandidate]
```

Candidates carry kind, topic, proposition, polarity, conditionality,
confidence, ambiguity, importance and UTF-8 byte offsets. Scores are finite
values in [0,1]. The Core remains authoritative for references, temporal scope,
contradictions, dependencies, state versions and durable mutation.

The runtime accepts `audio_duration_seconds` only as an explicit measurement
denominator. Without it, elapsed time is reported but RTF and pass/fail remain
null. This prevents text length or guessed speaking rate from being presented
as an audio RTF result.

## Model and performance profile

- Default artifact ceiling: 30 MiB (31,457,280 bytes), configurable for other
  deployment profiles.
- Default target: RTF < 0.01, strictly less than the threshold.
- Model SHA-256 and byte size are recorded with every execution.
- RTF = measured model-inference wall time / supplied source audio duration. This is an inference-only metric, not full SURGE pipeline RTF; packetization, scoring, graph validation and journal commit require a separate end-to-end benchmark.
- Benchmark summaries report sample count, mean RTF, maximum RTF and whether
  every measured sample met the target. No result is fabricated for missing
  durations.
- A production benchmark must additionally pin CPU model, core/thread count,
  runtime version, quantization, warm-up policy, input corpus digest, peak RSS,
  p50/p95/p99 latency and throughput. These are not inferred by this adapter.

## Operational sequence

1. Inspect and hash the model artifact.
2. Load it in a backend that implements `CompactModelBackend`.
3. Run recognition with the same source text snapshot used to create the packet.
4. Score and validate the returned candidates.
5. Build a ClassificationProposal and submit it through the existing packet
   admission/Core path.
6. Persist only after Core-side invariant and State Version validation.

## Current implementation limit

This commit supplies the interface, artifact guard, typed output contract,
timing/RTF measurement and deterministic fake-backend tests. It does not bundle
an ONNX model, inference library, trained weights, or a claim that any model
currently reaches RTF < 0.01. Hardware-specific performance certification is
the next gate.


## RecognitionPacket bridge

`build_recognition_packet` converts an execution into two coordinated but
separate artifacts:

1. A source-centric v0.9 RecognitionPacket, validated against the repository
   JSON Schema and anchored to the full NFC UTF-8 source digest.
2. A recognition proposal sidecar carrying typed model candidates and the same
   packet ID/source digest.

The sidecar is not an extension to the strict v0.9 packet schema. This keeps
model-specific outputs out of the durable source packet and lets downstream
scoring/validation evolve independently. Candidate offsets are byte offsets
into the NFC-normalized source. The runtime normalizes before inference so
candidate offsets and packet anchors share one coordinate system.

The bridge does not create SemanticDelta, Evidence, Claims, or Core writes.
The caller must pass the packet through RecognitionPacketAdapter and route
proposals through the established scorer and Core-authoritative transaction.
