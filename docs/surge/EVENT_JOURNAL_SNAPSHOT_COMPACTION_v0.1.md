# EventJournal snapshot and compaction design v0.1

Status: design contract only. No destructive compaction implementation is
claimed by this document.

## Why compaction is gated

SURGE's journal is shared by multiple independently replayed domains:
recognition packet admission, ContextGraph, retrieval tickets, temporal
boundaries/questions, evidence provenance, Chaos Cluster state, and retention
metadata. A snapshot that captures only one projection can silently discard
events required by another projection. Therefore a byte-level truncation or
segment rotation is prohibited until every journal consumer has a registered,
versioned snapshot/replay contract.

## Snapshot envelope

A future snapshot MUST include:

- snapshot format version and canonical serialization version;
- source journal format/version and source journal SHA-256;
- committed State Version and last included transaction/event identifiers;
- per-domain reducer identifier, reducer version, schema version, and replay
  watermark;
- canonical state digest per domain and a combined snapshot digest;
- counts of transactions, events, and domain records before and after replay;
- retention manifest references and tombstone metadata, without treating a
  tombstone as proof of physical deletion;
- creation time, tool/build identity, and provenance for the snapshot process.

All domain reducers must report that their replay watermark is identical to the
snapshot's committed State Version. Unknown event kinds must not be silently
discarded.

## Compaction protocol

Compaction is a copy-on-write protocol, never an in-place rewrite:

1. Acquire the same exclusive OS writer lock used by EventJournal.
2. Verify the source journal fully and create a byte-identical backup with a
   digest manifest.
3. Replay every registered domain and build the snapshot at one committed
   State Version.
4. Construct a new journal in a separate temporary path with a snapshot
   checkpoint and the exact post-checkpoint event tail.
5. Open the candidate through every registered reducer and compare each
   domain's canonical state digest, event/transaction counts, and State Version
   against the pre-compaction source.
6. Verify the candidate journal and backup digests; fsync candidate and parent
   directory where supported.
7. Atomically switch the active journal pointer/path while retaining the
   original journal and backup for rollback.
8. Reopen the switched journal and repeat all domain replay comparisons before
   releasing the writer lock.

Any failure leaves the original source authoritative. No fallback may
silently truncate the original.

## Retention and deletion boundaries

ARCHIVED is an active-working-set decision, not deletion. A payload tombstone
is an append-only proposal. Journal compaction may remove superseded journal
frames only after all domain state is represented in the verified snapshot;
it does not authorize deletion of referenced source payloads. Physical payload
deletion requires a separate authorization, legal/retention policy, backup
verification, and storage-specific erasure evidence.

## Required conformance before implementation

- Complete reducer registry for every event kind in the journal.
- Snapshot schema and deterministic canonical serializer with migration rules.
- Differential replay equivalence across all domains.
- Crash injection before/after candidate fsync, directory sync, and active-path
  switch.
- Recovery tests for orphan candidate files, stale manifests, partial switch,
  and rollback.
- Windows, macOS, and POSIX path/locking semantics.
- Fuzzing and sanitizer runs on snapshot parsing and replay.
- Large-journal memory, latency, and compaction-space measurements.

Until all gates pass, the supported operation is verified backup/restore plus
append-only journal retention metadata. Snapshot/compaction remains disabled.
