# SURGE Packet Admission Core v0.1

## Purpose

Join the trusted RecognitionPacket adapter to the C++ Core transaction boundary.
A packet is not visible to SourceSequencer or RangeLedger until its single
EventJournal transaction has committed. Failed validation or journal commit
leaves staged in-memory state unpublished.

## Authority boundary

The Python adapter validates the Draft 2020-12 packet schema, NFC-normalized
source snapshot digest and exact UTF-8 byte-range SHA-256. It emits a typed
ValidatedRecognitionPacket with both validation flags set. The C++ API rejects
an envelope without both flags and independently checks required identities,
offset basis, range bounds shape, sequence and revision metadata.

The C++ prototype does not recompute SHA-256 and cannot cryptographically prove
that a caller did not forge the validation flags. The coordinator API must
therefore only be called by the trusted in-process adapter bridge. Direct
untrusted callers must not be given this API.

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
- The trusted adapter is currently a Python module; an actual Python-to-C++
  binding/IPC bridge is not included in this milestone.
- C++ independently verifies SHA-256 but does not perform JSON Schema validation
  or Unicode NFC normalization. The caller must provide the normalized snapshot.
- A direct Python-to-C++ binding/IPC bridge is not included in this milestone.
- No cross-process locking, authorization policy engine, source authenticity,
  compaction or snapshotting.
- Journal is shared with other Core events; coordinator replay filters its
  own event kind and rebuilds only packet-admission state.
