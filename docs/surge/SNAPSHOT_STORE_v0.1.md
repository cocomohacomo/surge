# Durable Snapshot Store v0.1

## Purpose

Persist a validated SnapshotCodec envelope as a crash-consistent generation without changing or truncating the append-only EventJournal.

## Save protocol

1. Validate the complete envelope and enforce the configured maximum file size.
2. Write a same-directory `.tmp` file using bounded I/O and restrictive POSIX permissions where available.
3. Flush the temporary file to stable storage.
4. If the current generation is valid and backup retention is enabled, stream-copy it to `.bak.tmp`, flush it, atomically replace `.bak`, and sync the parent directory.
5. Atomically replace the current generation with the new temporary file.
6. Sync the parent directory on POSIX. Windows uses `MoveFileExW(MOVEFILE_WRITE_THROUGH)` for replacement durability.

A stale `.tmp` file is never considered committed. The current file is replaced only after the new generation has been fully written and synced. The previous known-good generation is not overwritten by a corrupt current file.

## Recovery

Load validates the current generation first. If it is missing, truncated, oversized, or fails the SnapshotCodec checksum/format validation, the store tries `.bak`. A successful backup recovery is reported through `used_backup=true`; it is read-only and does not silently rewrite the current generation.

If directory sync fails after atomic replacement, save reports failure because the durable outcome is uncertain. The caller must call load and inspect the returned generation before retrying.

## Resource and concurrency contract

- Default maximum generation size is 64 MiB; callers can lower or raise it explicitly.
- Backup copying uses 64 KiB chunks. Reading a generation is bounded by the configured file-size ceiling.
- SnapshotCodec metadata inspection validates the checksum and format without copying the opaque payload into a second document buffer; the load result retains the encoded bytes and returns metadata separately.
- Saves reject context changes, watermark regression, and divergent content at an identical watermark.
- One writer per snapshot path is required. The class mutex serializes calls through one instance; it is not a cross-process lock.
- Callers must quiesce the EventJournal and registered domains when capturing the bytes that are saved.
- The envelope checksum detects accidental corruption, not malicious modification or authenticity.

## Explicit non-goals

This store does not truncate, rotate, or compact EventJournal. It does not claim that a journal compaction operation is crash-safe. Copy-on-Write compaction remains blocked until snapshot+tail differential replay, file-generation recovery, and platform CI are verified.
