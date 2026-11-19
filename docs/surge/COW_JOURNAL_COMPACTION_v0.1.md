# Copy-on-Write Journal Compaction v0.1

## Purpose

Remove historical event payload frames only after a durable snapshot has been captured and verified. The journal's transaction watermark and idempotency identity indexes remain continuous across compaction.

## Compaction protocol

`SnapshotCoordinator::compact` requires all registered domains and the EventJournal to be quiescent for the operation.

1. Capture all registered domains at the current journal state version.
2. Save the SnapshotCodec envelope through SnapshotStore and read it back to verify the durable primary generation.
3. Mirror that exact snapshot into the SnapshotStore backup. Both snapshot generations now represent the same checkpoint watermark.
4. Build a new `SURGEJ2` journal file containing a checkpoint manifest with base state version, context ID, SHA-256 digest of the exact snapshot envelope, and the complete transaction/idempotency/event identity indexes.
5. Flush the new journal file, copy and sync the previous journal to `.bak`, atomically replace the current journal, and sync the parent directory.

If any step before current-journal replacement fails, the old journal remains active. If the final directory sync fails after replacement, the operation reports an uncertain durability outcome and closes the in-memory journal; reopen and inspect the checkpoint before continuing.

## Recovery

A J2 journal starts at its checkpoint base watermark and contains only later committed events. The manifest restores the historical transaction IDs, idempotency fingerprints, and event IDs, so duplicate retries and identity reuse checks remain effective after compaction.

Use `SnapshotCoordinator::restore_from_store` to load the matching snapshot and apply the bounded journal tail. When the snapshot watermark equals the J2 base, its context and exact envelope digest must match the journal manifest. A checkpoint older than the compacted base is rejected. If the SnapshotStore primary is corrupt, its mirrored backup has the same checkpoint watermark and can be used with the current journal tail.

The old journal generation remains at `.bak` as an independent full replay source through the compaction watermark. It is not silently substituted for a newer journal tail.

## Resource bounds and failure behavior

- The compacted identity manifest is one CRC-protected frame and is capped by the journal's 16 MiB frame limit. If identity history no longer fits, compaction fails without replacing the current journal.
- Historical event payloads are removed from the current journal and in-memory event vector. Identity indexes remain, preserving exact idempotency and event-ID uniqueness.
- `max_events` and `max_payload_bytes` apply to the active tail after compaction. `max_transactions` continues to count retained transaction identities across the journal lifetime.
- `EventJournalLimits::max_identity_bytes` optionally caps the retained identity-index budget across idempotency keys, transaction IDs, and event IDs. The accounting includes original key bytes, fingerprint bytes where applicable, and a conservative 128-byte per-entry structural allowance. Zero preserves the historical unlimited behavior.
- `identity_memory_bytes()` exposes the current logical budget usage. This is a conservative accounting metric, not a process-RSS measurement or allocator-exact byte count. The limit is checked while decoding compacted manifests and before accepting a new commit, so configured limits fail closed before the index grows beyond its budget.
- The snapshot envelope and backup journal are copied/written using bounded or streaming file operations; compaction does not require loading the old journal into memory.
- Compaction does not claim to bound total process RSS. The identity maps remain proportional to historical transaction/event identities.
- One writer lock and application-level quiescence are required.

## Conformance

The integrated test creates an earlier snapshot, appends further transactions, compacts at a later watermark, appends a new tail transaction, reopens the J2 journal, and restores snapshot plus tail. It verifies historical idempotency and event-ID rejection, snapshot-backup recovery, retained full-journal replay, and journal file-size reduction.

## Non-goals

This feature does not prune historical identity indexes, implement approximate Bloom-filter deduplication, or remove the `.bak` generation. Identity-index expiry would change exactly-once semantics and requires a separate explicit retention policy.
