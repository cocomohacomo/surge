# SURGE Persistence Durability Contract v0.1

Date: 2026-11-21

This contract describes the durability barriers implemented by the current
EventJournal and SnapshotStore. It is a local-filesystem contract, not a claim
that all filesystems or storage devices provide identical guarantees.

## Writer ownership

- A journal and each snapshot path have exactly one active writer process at a time.
- The writer lock is an operating-system lock on the adjacent `.lock` file.
  POSIX uses non-blocking exclusive `flock`; Windows opens the lock file with
  zero sharing.
- A competing process must fail to open the journal with
  `JOURNAL_WRITER_LOCKED`. It must not delete or replace the lock file while
  another process may own it.
- Snapshot writes use the same adjacent `.lock` convention and exclusive OS lock. Separate SnapshotStore instances and processes cannot replace the same generation concurrently.
- Multiple readers are not a supported consistency contract while a journal writer is
  active. Snapshot readers may read atomically replaced generations and verify the
  primary or backup, but should prefer quiescent backups for forensic inspection.
- Lock correctness on NFS, SMB, cloud-synchronized folders, FUSE, and other
  network or user-space filesystems is not guaranteed. Use a local filesystem.

## EventJournal commit durability

A transaction is framed as BEGIN, zero or more EVENT frames, and a COMMIT
frame. Each appended frame is flushed through the C++ stream, closed, and
synchronized to stable storage before the next frame is appended.

- POSIX: each frame uses `fsync` on the journal file. Initial journal creation
  additionally synchronizes the containing directory.
- Windows: each frame uses `FlushFileBuffers` on the journal file handle.
- A successful commit is acknowledged only after the COMMIT frame's durability
  barrier succeeds.
- A failure before the COMMIT frame is durable is treated as uncommitted on
  replay. If the COMMIT frame was written but its sync result was ambiguous,
  reopen and retry with the exact same idempotency key and transaction payload
  to resolve the outcome without duplicate application.
- CRC32 detects accidental frame corruption; it does not authenticate content.

## SnapshotStore replacement durability

Snapshot bytes are written to a temporary sibling file, flushed, verified,
and atomically renamed over the primary generation.

- POSIX: the temporary file is `fsync`ed before rename; after rename the parent
  directory is `fsync`ed. This is the supported durable replacement sequence.
- Windows: the temporary file is flushed with `FlushFileBuffers`; replacement
  uses `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.
  A portable directory-handle flush is not available in this implementation.
- A verified previous generation is retained as `.bak` when backup retention
  is enabled. Restore verifies the primary before exposing payload bytes and
  falls back to the verified backup.
- A successful return means all barriers available through the platform API
  completed. It cannot protect against dishonest device caches, controller
  firmware that violates flush semantics, media failure, or filesystem bugs.

## Supported deployment assumptions

1. Local NTFS on Windows, or local POSIX filesystems with working `fsync`,
   atomic same-filesystem rename, and directory synchronization.
2. Journal, lock, temporary, primary, and backup files reside on the same
   filesystem.
3. Storage hardware honors flush requests.
4. The application does not manually modify journal or snapshot files.

Network filesystems and removable media with unknown write-cache behavior
require independent qualification and are not covered by this contract.

## Recovery test obligations

CI exercises snapshot writer exclusion, pre-rename and post-rename failure
injection, frame append/sync fault injection, truncated journal tails,
checksum corruption, competing writer processes, interrupted snapshot streams,
corrupt primary fallback, and atomic replacement on Linux, macOS, and Windows.
These tests validate software recovery paths. They do not simulate physical
power removal or prove hardware-level persistence.

For production qualification, run filesystem-specific crash tests on the
actual deployment image and storage device, then archive the test method,
filesystem mount/options, device model/firmware, and recovery result.
