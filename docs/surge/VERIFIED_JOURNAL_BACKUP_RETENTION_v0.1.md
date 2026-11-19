# Verified Journal Backup Retention v0.1

## Purpose

Copy-on-write compaction retains the previous full journal as `.bak`. This
module provides an explicit, fail-closed operation to remove that generation
only after validating the active compacted journal and both snapshot recovery
generations.

## Safety gates

The operation requires:

1. The active journal is open and reports a compacted checkpoint.
2. The primary snapshot generation is valid, has the same context, has equal
   state/journal watermarks, and is at or beyond the compacted base. At the
   exact base watermark its envelope SHA-256 must match the journal manifest.
3. The snapshot backup is independently readable and satisfies the same
   checkpoint conditions. A valid primary alone is not sufficient.
4. At least one of the two snapshot generations must be the exact compacted
   base envelope, verified by SHA-256. Two later snapshots without the exact
   base copy are not sufficient evidence for pruning.
5. The retained journal backup is a regular file, opens as a non-compacted
   journal, and ends exactly at the compacted base watermark.

Only then is `journal.bak` removed and its parent directory synchronized.
Any failed precondition leaves the retained journal generation untouched.
Directory-sync failure is reported as an uncertain durability outcome because
the unlink may already have occurred.

## Recovery contract

After pruning, recovery relies on the active compacted journal plus the
SnapshotStore primary or backup generation. The retained pre-compaction full
journal is no longer available. Applications must continue to use
`SnapshotCoordinator::restore_from_store` for domain restoration.

## Explicit non-goals

This operation is not automatic, does not delete snapshot backups, does not
prune transaction/idempotency/event identity indexes, and does not provide
multi-process coordination beyond the active journal's existing writer lock.
The application must quiesce writers and must call this only after compaction
has completed successfully.
