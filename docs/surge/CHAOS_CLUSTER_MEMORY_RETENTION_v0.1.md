# Chaos Cluster memory retention and recovery v0.1

## Scope

This increment defines an auditable retention planner for derived Chaos Cluster
memory projections. It does not compact, truncate, rewrite, or delete the
authoritative EventJournal. The journal remains the source of truth.

## Retention model

Each record is assigned a deterministic retention score from independent
factors:

| Factor | Default weight | Meaning |
|---|---:|---|
| Salience | 0.30 | Importance of the information |
| Utility | 0.25 | Expected future usefulness |
| Interest | 0.20 | Current attention/interest |
| Dependency | 0.15 | Number/strength of dependent context paths, normalized by caller |
| Recency | 0.10 | Exponential decay with a 30-day scale |

The weights are configurable but must be finite, in [0,1], and sum to one.
Coherence, organization, and semantic neatness are deliberately excluded as
eviction factors. A low-coherence/high-salience record can survive when its
other factors warrant retention.

The planner emits ACTIVE and ARCHIVED states, factor breakdown, policy version,
stable ranking, source digests, provenance references, lineage references, and
a canonical SHA-256 manifest digest. Equal scores are ordered by immutable
record ID. The manifest is reproducible for identical inputs.

## Archival, tombstones, and restore

ARCHIVED means excluded from the active working set, not deleted. The manifest
preserves identity and provenance metadata. A payload tombstone is a separate,
explicit, append-only event proposal bound to the exact manifest digest,
record ID, source digest, actor, reason, policy version, and timestamp.
Creating or journaling a tombstone proposal does not physically erase bytes and
must never be reported as proof of erasure.

Physical deletion remains outside this module and requires an external
retention policy, authorization, backup/restore policy, and storage-specific
deletion verification. Restore from a manifest alone restores metadata and
identity, not deleted payload bytes. Payload restoration requires a verified
backup containing source bytes.

## EventJournal integration

The C ABI exposes a retention-only transaction surface:
- `surge_retention_journal_commit` accepts exactly one retention event per journal
  transaction, preserving the journal's one-transaction/one-State-Version replay contract and allowlists only `RETENTION_MANIFEST_RECORDED` and
  `RETENTION_TOMBSTONE_PROPOSED`.
- `surge_retention_journal_get_events` projects only those event kinds.
- `SemanticCoreBridge.commit_retention_events` and
  `SemanticCoreBridge.get_retention_events` provide the Python bridge.
- `persist_retention_manifest` and `persist_payload_tombstone` bind events
  to deterministic event IDs derived from their SHA-256 digests.
- `replay_retention_projection` verifies event IDs, manifest and tombstone
  digests, archived targets, and source-digest agreement before rebuilding the
  projection.

Retention events share the journal's global State Version and transaction
idempotency. Unknown or domain-semantic event kinds are rejected by the
retention-only C ABI. The projection can be rebuilt from the journal after
restart. The existing standalone manifest file remains an optional atomic
cache/export, not the authoritative record.

## Crash consistency

Manifest file writes use a same-directory temporary file, fsync, atomic
replace, and directory fsync on POSIX. Loading verifies schema version, record
counts, states, and canonical digest. Journal commits use EventJournal's
commit marker and stable-storage sync. A torn/incomplete transaction is not
visible during replay. Platform-specific power-loss limits remain as described
in JOURNAL_OPERATIONS_v0.1.md.

## API

- `build_retention_manifest(records, capacity, policy_version, now, weights)`
- `verify_retention_manifest(manifest)`
- `create_payload_tombstone(record_id, manifest, reason, actor, policy_version, created_at)`
- `persist_retention_manifest(core, manifest, transaction_id, idempotency_key, expected_state_version)`
- `persist_payload_tombstone(core, event, transaction_id, idempotency_key, expected_state_version)`
- `replay_retention_projection(events)`
- `write_retention_manifest(path, manifest)`
- `load_retention_manifest(path)`

## Conformance

Python tests cover deterministic ordering, capacity planning, salience
preservation, coherence independence, digest tampering, explicit tombstone
semantics, atomic round-trip, invalid weights, invalid scores, timezone
validation, C ABI allowlisting, atomic journal commit, idempotent retry,
tombstone/manifest linkage, and restart replay. This is journaled retention
metadata, not a completed physical compaction or deletion implementation.
