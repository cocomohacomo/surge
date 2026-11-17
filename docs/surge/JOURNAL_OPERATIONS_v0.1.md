# EventJournal Operations and Migration v0.1

## Current operating contract

EventJournal remains a single-writer append-only journal. This increment adds:
- an OS-visible sidecar lock at `<journal>.lock`, exclusive and non-blocking;
- refusal to open a journal when another writer owns the lock;
- `fsync` after every complete frame on POSIX and `FlushFileBuffers` on Windows;
- file and parent-directory synchronization when the journal header is first created;
- a lock held for the EventJournal object's lifetime, released on destruction.

The in-process mutex still protects threads in one process. The OS lock protects competing processes. Opening a second EventJournal as a writer is intentionally rejected. POSIX lock/sync and the backup/restore tool are exercised in CI; Windows/macOS runtime parity remains unverified.

## Backup and restore

`tools/journal_backup.py` supports:
- `backup JOURNAL DESTINATION`: acquire the same writer lock, copy to a temporary file, fsync, atomically rename, and write a SHA-256/size manifest;
- `verify BACKUP`: verify the manifest, format header, byte count and digest;
- `restore BACKUP DESTINATION [--replace]`: verify first, acquire the destination writer lock, and atomically restore. Existing destinations are never overwritten without explicit `--replace`.

After restore, reopen the file with EventJournal and run full replay/conformance before serving traffic. A matching digest proves byte identity, not semantic validity.

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

- cross-platform CI for Windows lock/sync and macOS durability behavior;
- automated backup scheduling/retention and off-host replication;
- crash-injection tests at OS sync boundaries and restore drills;
- snapshot/compaction and journal segment rotation;
- a versioned migration registry and executable migration tooling;
- authenticated/encrypted backups and key rotation;
- operational metrics for lock contention, fsync latency, journal growth and restore time.
