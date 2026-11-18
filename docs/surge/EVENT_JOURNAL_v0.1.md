# SURGE Event Journal v0.1

## Purpose

Provide the first durable append-only transaction substrate for the C++ Core prototype. This component stores opaque domain events; it does not itself interpret Claims, Clusters, Questions or Relations.

## Frame format

- File header: ASCII `SURGEJ1\n`.
- Each frame: little-endian uint32 payload length, UTF-8 frame payload, little-endian CRC-32/ISO-HDLC.
- Maximum frame payload: 16 MiB.
- Record types:
  - `B`: transaction begin, transaction ID, idempotency key, expected state version, request fingerprint.
  - `E`: event, transaction ID, immutable event ID, event kind, opaque payload.
  - `C`: commit marker, transaction ID, resulting state version, event count, request fingerprint.

Frames are append-only. A transaction is visible only after a valid C frame. A partial trailing frame or an uncommitted B/E sequence is ignored during recovery. A complete frame with checksum mismatch or invalid transaction sequence fails recovery closed.

## Transaction rules

- State version is monotonic and increments once per committed transaction.
- Expected version must equal current state version.
- Event IDs are globally unique within the journal.
- Idempotency key + identical request fingerprint returns the original logical success without appending.
- Reusing an idempotency key with a different request is rejected.
- Transaction IDs cannot be reused.
- All events in a transaction are committed together or none are visible after recovery.
- The in-process API serializes calls with a mutex. Multi-process writers are not supported.

## Integrity and durability limits

CRC-32 detects accidental frame corruption; it is not a cryptographic authenticity mechanism. The idempotency fingerprint uses FNV-1a and is not cryptographic. Applications requiring adversarial tamper evidence must add a cryptographic MAC/signature and trusted key management.

The implementation flushes each C++ stream frame and then calls `fsync` on POSIX or `FlushFileBuffers` on Windows before reporting the append as successful. Initial journal creation also synchronizes the file and, on POSIX, its parent directory. This is stronger than stream flush alone, but it is not a universal guarantee against every storage-controller, filesystem, virtualized-volume, or hardware failure. Windows parent-directory metadata durability is not explicitly synchronized. Power-loss guarantees must therefore be stated per supported platform/filesystem and validated with platform-specific fault testing.

## Explicit non-goals

- Cross-process locking.
- Snapshot compaction.
- Encryption at rest.
- Semantic validation of event payloads.
- Automatic replay of domain state beyond returning the ordered committed event list.
