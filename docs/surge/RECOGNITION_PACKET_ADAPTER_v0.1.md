# RecognitionPacket Adapter v0.1

## Responsibility boundary

The adapter is the trusted input boundary between source-specific adapters and
the SURGE Core. It validates JSON shape and verifies that source offsets and
digests refer to one immutable NFC-normalized UTF-8 source snapshot. It does
not infer labels, create Evidence, update ContextGraph, or authorize commits.

## Admission sequence

1. Validate the complete RecognitionPacket against
   `schemas/surge-v0.9-input.schema.json` (JSON Schema Draft 2020-12).
2. Normalize the supplied immutable source snapshot to Unicode NFC and encode
   it as UTF-8.
3. Verify `content.normalized_digest` against SHA-256 of the complete snapshot.
4. Verify the half-open `[start_offset,end_offset)` range is in bounds.
5. Verify `source_range_digest` against SHA-256 of exactly those bytes.
6. Enforce packet, idempotency, source sequence, range and revision lineage
   uniqueness before producing an Admission record.

## Identity

- Stream: `(context_id, source_id, source_generation)`.
- Sequence: stream identity plus `source_sequence`.
- Range: stream identity, block_ref, offset_basis and half-open byte offsets.
- Revision: a new packet with `finalization=REVISION`, a strictly identified
  predecessor reference and a positive source_revision_sequence.

The adapter never treats changed bytes as a duplicate. An existing range can
only be extended through explicit revision lineage. Original records remain
immutable.

## Result and errors

Successful first admission returns `ADMITTED`; an exact idempotent retry
returns the original Admission. Rejected cases include SCHEMA_INVALID,
SOURCE_SNAPSHOT_DIGEST_MISMATCH, SOURCE_RANGE_OUT_OF_BOUNDS,
SOURCE_RANGE_DIGEST_MISMATCH, IDEMPOTENCY_KEY_REUSED,
SOURCE_SEQUENCE_REUSED, RANGE_CONTENT_CONFLICT, RANGE_ALREADY_ADMITTED,
REVISION_LINEAGE_REQUIRED, REVISION_TARGET_MISMATCH,
SOURCE_REVISION_SEQUENCE_REUSED and REVISION_TARGET_NOT_FOUND.

## Security and durability limits

The Python registry is process-local and is a conformance adapter, not a
durable ledger or authorization system. The immutable source snapshot must be
provided by a trusted source adapter; this module cannot prove that the caller
obtained it from an authentic origin. Durable sequencing, authorization,
audit provenance, transaction integration and restart recovery remain Core
responsibilities. The current prototype does not yet connect this Admission
record to a C++ packet decoder or atomically persist it with EventJournal.
