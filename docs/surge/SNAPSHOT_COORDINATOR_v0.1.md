# Snapshot Coordinator and Domain Restore Contract v0.1

## Domain adapter

Each registered state domain supplies a stable domain ID, an exact schema ID, deterministic capture bytes, and a two-phase restore implementation. `prepare_restore` must validate and allocate all replacement state without mutating live state. It returns a prepared object whose `commit()` is noexcept and performs only the already-prepared state swap.

The coordinator sorts domains by ID to make bundle order deterministic. Duplicate IDs, missing domains, extra domains, schema mismatch, context mismatch, truncated data, and watermark regression fail closed.

## Atomicity boundary

All domains must prepare successfully before the coordinator calls any commit. This gives atomic cross-domain publication provided adapters honor the contract: prepare is side-effect free, and commit is noexcept/non-failing. Adapters must use preallocated state and swap ownership during commit. A domain that cannot satisfy this contract must not be registered for coordinated restore.

## Bundle encoding

The outer SnapshotCodec envelope carries context ID, state version, journal sequence, and SHA-256 integrity. Its payload contains a u32 domain count followed by sorted entries. Each entry has u32-length-prefixed UTF-8 domain ID, schema ID, and opaque domain payload. Per-domain payloads are bounded by the coordinator.

## Replay and compaction are still gated

The coordinator does not open or truncate journals. The caller must verify that the journal tail begins after the snapshot sequence, has no gaps, and belongs to the same context. Differential replay equivalence and provenance/reference-integrity checks remain mandatory before compaction is enabled.

Semantic forgetting remains a domain-level state transition. Storage compaction preserves the resulting committed state and provenance; it does not independently decide what should be forgotten.
