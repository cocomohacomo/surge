# Explicit Journal Backup Retention v0.1

## Purpose

Copy-on-write compaction keeps the previous full journal at `<journal>.bak`. This is a useful independent replay source, but it also means compaction does not reclaim the old journal's disk space. This feature adds an explicit, fail-closed operation to remove that generation after the compacted journal has a verified snapshot recovery anchor.

## API

`EventJournal::prune_backup_after_checkpoint(const SnapshotStore&, error)`

The operation is deliberately explicit. Compaction never prunes the backup automatically.

## Preconditions

The operation succeeds only when all of the following hold:

1. The EventJournal is open and has completed COW compaction.
2. The SnapshotStore can load a valid generation, either primary or backup.
3. Snapshot context ID equals the compacted journal context ID.
4. Snapshot state version and journal sequence both equal the compacted base watermark.
5. SHA-256 of the exact loaded snapshot envelope equals the digest stored in the J2 checkpoint manifest.
6. The journal backup path is a regular file (or is already absent).

Any mismatch or unavailable snapshot fails closed and leaves the journal backup untouched. A symlink or non-regular path is rejected. After deletion, the parent directory is synchronized on POSIX. On Windows, the deletion is checked, but portable directory-handle flushing is not available through the implementation's supported API.

## Recovery trade-off

After successful pruning, the old full journal is no longer available as an independent replay source. Recovery instead depends on the exact checkpoint snapshot plus the current compacted journal tail. The operation is idempotent when the backup is already absent.

This API does not delete the SnapshotStore backup. The snapshot primary and mirrored backup remain available for checkpoint recovery.

## Conformance

The dedicated test verifies:
- a valid but divergent snapshot cannot authorize deletion;
- the exact checkpoint envelope can authorize deletion;
- a second prune call is idempotent;
- snapshot plus compacted journal still restores the domain after pruning.

## Operational guidance

Retain the full journal backup when independent historical replay is required. Prune it only when disk reclamation is needed and the application accepts the loss of that replay path. Applications should keep their own external backup policy for disaster recovery.
