# SURGE Packet Admission Core v0.1

## Purpose

Join the trusted RecognitionPacket adapter to the C++ Core transaction boundary.
A packet is not visible to SourceSequencer or RangeLedger until its single
EventJournal transaction has committed. Failed validation or journal commit
leaves staged in-memory state unpublished.

## Authority boundary

The Python adapter validates the Draft 2020-12 packet schema, performs NFC
normalization, and verifies byte-offset bounds and expected digests. It passes
the normalized source bytes and packet metadata through a stable C ABI loaded
by Python ctypes. The C++ Core independently validates UTF-8 and recomputes
SHA-256 over the complete snapshot and exact half-open range before staging
any state. The C++ coordinator is the sole durable admission authority.
Production integration uses `RecognitionPacketAdapter.admit_to_core()`;
the legacy in-memory `admit()` method remains available for isolated adapter
tests and must not be treated as durable Core admission.

## Atomic admission sequence

1. Require coordinator recovery to have completed.
2. Validate Core-level required fields, UTF-8 validity, full snapshot digest and exact range digest.
3. Check packet ID, idempotency key and per-stream source sequence uniqueness.
4. Copy the stream's SourceSequencer and RangeLedger into staged state.
5. Apply range/revision validation and source sequence observation to staged state.
6. Commit one immutable RECOGNITION_PACKET_ADMITTED event through EventJournal
   using the packet idempotency key and current journal state version.
7. Only after a successful non-duplicate journal commit, publish staged state
   and update packet/idempotency indexes in memory.

A failed journal commit does not publish the staged sequence/range changes.
An exact packet retry is recognized from the recovered packet index and does
not append a second journal event.

## Replay

On startup, call EventJournal::open(), then PacketAdmissionCoordinator::recover().
The coordinator replays committed RECOGNITION_PACKET_ADMITTED events in journal
order and reconstructs per-stream SourceSequencer, RangeLedger, packet identity
and idempotency indexes. A malformed or semantically inconsistent committed
admission event fails recovery closed.

## Event payload

The journal payload uses a versioned RPA2 encoding with quoted string fields.
The decoder remains backward-compatible with RPA1 records created before
independent snapshot digest verification; historical RPA1 events are trusted
as already committed records during replay.
The event contains packet/source identity, normalized byte range, range digest,
canonical packet digest and revision metadata. Source text is not copied into
the journal. EventJournal CRC protects against accidental frame corruption,
not malicious modification.

## Limits

- Single-process only, matching EventJournal's mutex boundary.
- EventJournal flush is not an OS fsync/FlushFileBuffers power-loss guarantee.
- Python-to-C++ integration uses a stable C ABI and ctypes; it is in-process
  and single-process, not an IPC or multi-process transport.
- C++ independently verifies SHA-256 and UTF-8 but does not perform JSON Schema
  validation or Unicode NFC normalization. Those remain Python adapter duties.
- No cross-process locking, authorization policy engine, source authenticity,
  compaction or snapshotting.
- Journal is shared with other Core events; coordinator replay filters its
  own event kind and rebuilds only packet-admission state.
