# Atomic Snapshot + Journal Tail Replay v0.1

## Purpose

Restore all registered cognitive domains from one checkpoint and replay the append-only journal suffix without publishing partially restored state. This is the prerequisite for differential replay verification and, later, safe storage compaction.

## API

`SnapshotCoordinator::restore_with_tail` accepts:

- a checksum-verified SnapshotCodec envelope;
- the open EventJournal used by the registered domains;
- expected context ID and minimum state/journal watermarks;
- explicit `JournalTailLimits` for maximum event count and aggregate event bytes.

The checkpoint's `state_version` and `journal_sequence` must match. EventJournal currently uses a single transaction watermark for both. The coordinator obtains the entire suffix with `committed_tail`; that API fails closed on future watermarks, gaps, event-count limits, byte limits, and unavailable journals.

Each native adapter decodes the checkpoint into detached state and applies only its own event kinds from the same ordered tail. Foreign-domain events still advance the shared journal watermark but do not mutate that adapter. The ContextGraph adapter also restores DELTA_META idempotency fingerprints and handles standalone Question-memory and temporal-reassessment events.

## Atomicity contract

Every adapter must finish parsing, validation, allocation, and replay in `prepare_restore_with_tail`. Only after every domain has returned a prepared state does the coordinator invoke noexcept commits. A malformed event or resource-limit failure therefore leaves all live domains unchanged.

The final journal watermark is checked again immediately before publication. This catches accidental concurrent commits during staging, but it is not a replacement for a global application lock. Callers must quiesce all readers and writers across the journal and every registered domain for the entire capture/restore operation.

## Native domains

- ContextGraph: semantic delta events, idempotency metadata, question-memory events, and temporal reassessment.
- Evidence Provenance: ordered evidence revisions with event watermark matching.
- Chaos Cluster: latest versioned full-state event.
- Temporal Boundary: contribution, clear, and candidate-selection events.
- Temporal Question: versioned question snapshot events.

## Differential conformance

The integrated test creates a checkpoint, mutates all five domains, restores checkpoint plus bounded tail, then independently copies the journal and performs full replay into fresh domain instances. The canonical SnapshotCodec payloads must match byte-for-byte. It also verifies that a too-small tail budget and a malformed provenance event fail without partial publication.

## Explicit non-goals and release gates

This feature does not truncate or compact the journal, persist snapshot files atomically, or claim crash consistency across filesystem replacement. The snapshot codec's checksum is an integrity check, not authentication. Tail limits bound copied event count and payload bytes, not total process RSS. Snapshot plus tail must pass the differential test and platform CI before any copy-on-write compaction is considered.
