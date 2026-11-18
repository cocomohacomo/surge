# EventJournal Operations and Migration v0.1

## Current operating contract

EventJournal remains a single-writer append-only journal. The writer lock is an OS-visible sidecar lock at `<journal>.lock`, held for the EventJournal object's lifetime. A second writer is rejected.

- POSIX: C++ and Python acquire an exclusive non-blocking `flock(LOCK_EX | LOCK_NB)` on the same sidecar inode.
- Windows: C++ and Python both open the sidecar with `CreateFile` / `CreateFileW` and share mode zero. This is the same Windows sharing-exclusion mechanism. Python must not use `msvcrt.locking` here because byte-range locks are a separate lock namespace and do not exclude the C++ writer.
- POSIX appends are followed by `fsync`; Windows appends are followed by `FlushFileBuffers`.
- Header creation syncs the journal file and parent directory where supported. Windows currently syncs the file but does not claim portable parent-directory durability.
- Recovery tail truncation is synced before the journal is considered recovered.

The in-process mutex protects threads in one process. The OS lock protects competing processes and backup/restore operations. Do not delete the sidecar lock file while any process may hold it.

## Commit outcome after a failed sync barrier

A failed stable-storage barrier has two different meanings depending on which
frame failed:

- If BEGIN or an EVENT frame fails to synchronize, no COMMIT marker has been
  issued. The transaction is not committed; recovery discards the incomplete
  tail and the caller may retry.
- If the COMMIT frame has been written but its `fsync` /
  `FlushFileBuffers` barrier fails, the result is
  `JOURNAL_COMMIT_OUTCOME_UNKNOWN`. The frame may be present after restart or
  may have been lost in a power failure. The caller MUST reopen/recover the
  journal and retry with the exact same transaction ID, idempotency key,
  expected State Version, and event payloads. Recovery either reports the
  original transaction as a duplicate or permits the exact retry to commit.

Do not generate a new idempotency key after an ambiguous commit result. That
could duplicate a transaction whose COMMIT marker survived. The
`SyncFrameGate` test seam exercises pre-COMMIT sync failure and ambiguous
COMMIT-marker sync failure without claiming to simulate physical power loss.

## Backup and restore

`tools/journal_backup.py` supports:
- `backup JOURNAL DESTINATION`: acquire the same writer lock, copy to a temporary file, fsync, atomically rename, and write a SHA-256/size manifest;
- `verify BACKUP`: verify the manifest, format header, byte count, SHA-256 digest, every frame CRC, transaction ordering, State Version progression, idempotency uniqueness, event-ID uniqueness, and request fingerprints;
- `restore BACKUP DESTINATION [--replace]`: perform the same non-mutating replay validation first, acquire the destination writer lock, and atomically restore. Existing destinations are never overwritten without explicit `--replace`.

Backup creation and verification reject a torn or uncommitted recoverable tail rather than silently normalizing it. A complete frame with a bad checksum or invalid transaction sequence fails closed. The manifest records replay State Version, committed transaction count, and committed event count; these are compared against a fresh replay during verification. The validator mirrors the EventJournal v1 framing and transaction invariants in Python. After restore, reopen the file with the native C++ EventJournal and run full conformance before serving traffic. SHA-256 proves byte identity against the manifest, not authenticity if an attacker can replace both the backup and its manifest.

## Retention metadata transactions

Retention manifests and tombstone proposals can be appended through the
retention-only C ABI, one event per transaction so every journal consumer can
advance its global State Version exactly once per transaction. These events participate in the same global journal
State Version and idempotency rules as other transactions. Only
`RETENTION_MANIFEST_RECORDED` and `RETENTION_TOMBSTONE_PROPOSED` are accepted
through this surface; arbitrary graph/domain event kinds are rejected.
`surge_retention_journal_get_events` returns the verified journal's retained
metadata event stream, and the Python projection rebuild validates canonical
SHA-256 digests, event-ID binding, archived targets, and source-digest
consistency. This records decisions, not physical erasure. Manifest JSON files
are optional cache/export copies and can be rebuilt from journal events.

## Migration policy

No general schema migrator is claimed implemented yet. Until one exists:
1. stop the writer and acquire the journal lock;
2. create and verify a backup;
3. migrate a copy, never the only source;
4. require a declared source/target format version and deterministic migration ID;
5. replay the migrated copy and compare event count, transaction count, State Version and domain projection;
6. retain the source backup and migration manifest;
7. switch paths atomically only after verification; rollback by restoring the previous file.

In-place mutation, implicit downgrade and migration without a verified backup are prohibited. The current format is `SURGEJ1`; migration of future journal versions remains a separate task.

## Remaining production gaps

- automated backup scheduling/retention and off-host replication;
- crash-injection tests at OS sync boundaries and restore drills;
- snapshot/compaction and journal segment rotation;
- a versioned migration registry and executable migration tooling;
- authenticated/encrypted backups and key rotation;
- operational metrics for lock contention, fsync latency, journal growth and restore time.


## Retention integration limits

Journaled retention decisions are append-only metadata. They do not yet
rewrite or compact journal segments, snapshot domain state, physically delete
payloads, or perform cryptographic erasure. Those operations require a
separate verified snapshot/compaction design and crash-recovery drills.
