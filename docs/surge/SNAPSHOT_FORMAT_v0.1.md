# SURGE Snapshot Format v0.1

Status: implemented codec foundation; persistence, domain reducers, restore orchestration, and compaction are not yet enabled.

## Purpose

Provide a bounded, versioned, domain-neutral envelope for serializing a consistent SURGE context state. Domain serializers own the payload. The envelope records the schema identity, context identity, state version, and journal sequence represented by that payload.

## Binary envelope

All integers are unsigned big-endian.

| Field | Encoding |
|---|---|
| Magic | 8 bytes: `SURGESNP` |
| Format version | u32, currently 1 |
| Schema ID | u32 byte length + UTF-8 bytes |
| Context ID | u32 byte length + UTF-8 bytes |
| State version | u64 |
| Journal sequence | u64 |
| Payload | u32 byte length + opaque bytes |
| Integrity | 64 lowercase ASCII hex characters, SHA-256 of all preceding bytes |

The payload is opaque and may contain binary data. The codec does not assume JSON, rewrite semantic records, or infer missing values.

## Safety contract

- Default total envelope ceiling: 64 MiB.
- Default payload ceiling: 60 MiB.
- Schema and context identifiers are limited to 4 KiB each.
- Decoder checks total size, lengths, supported format version, exact payload boundary, and SHA-256 before publishing decoded output.
- Decode uses a temporary candidate and leaves caller output unchanged on failure.
- SHA-256 detects accidental corruption; it is not an authenticity signature.
- A snapshot is valid only for its exact schema ID and context. Callers must additionally verify schema compatibility and that the journal tail starts strictly after the snapshot's journal sequence.
- A codec success is not permission to truncate, delete, or compact the source journal.

## Restore and compaction gate

Before enabling restore or compaction, each state domain must provide deterministic serialization and restore, schema migration policy, referential-integrity validation, and a differential replay test proving that snapshot + journal tail yields the same canonical state as full replay. The coordinator must reject gaps, overlapping journal ranges, wrong contexts, unsupported schemas, and resource-budget violations.

Semantic forgetting remains independent: a domain may intentionally lower the salience or retire a semantic record, but storage compaction must preserve the resulting committed state and its provenance lineage.
