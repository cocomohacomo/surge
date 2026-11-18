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
| Recency | 0.10 | Exponential decay with a 30-day half-life-like scale |

The weights are configurable but must be finite, in [0,1], and sum to one.
Coherence, organization, and semantic neatness are deliberately excluded as
eviction factors. A low-coherence/high-salience record must survive when its
other factors warrant retention.

The planner emits ACTIVE and ARCHIVED states, a complete factor breakdown,
policy version, stable ranking, source digests, provenance references, lineage
references, and a canonical SHA-256 manifest digest. Equal scores are ordered
by immutable record ID. The manifest is reproducible for identical inputs.

## Archival, tombstones, and restore

ARCHIVED means excluded from the active working set, not deleted. The manifest
preserves identity and provenance metadata. A payload tombstone is a separate,
explicit, append-only event proposal bound to the exact manifest digest,
record ID, source digest, actor, reason, policy version, and timestamp.
Creating a tombstone proposal does not physically erase bytes and must not be
reported as proof of erasure.

Physical deletion is outside this module and requires an external retention
policy, authorization, backup/restore policy, and storage-specific deletion
verification. Restore from a manifest alone can restore metadata and identity,
not deleted payload bytes. Payload restoration requires a verified backup
containing the source bytes.

## Crash consistency

Manifest writes use a same-directory temporary file, fsync, atomic replace,
and directory fsync on POSIX. Loading verifies schema version, record counts,
states, and the canonical digest. The manifest is an auxiliary projection and
can be rebuilt from authoritative journal state.

## API

- `build_retention_manifest(records, capacity, policy_version, now, weights)`
- `verify_retention_manifest(manifest)`
- `write_retention_manifest(path, manifest)`
- `load_retention_manifest(path)`
- `create_payload_tombstone(record_id, manifest, reason, actor, policy_version, created_at)`

The caller must persist tombstone proposals through the normal journal transaction
path. This module intentionally has no direct mutation API for EventJournal.

## Conformance

The Python tests cover deterministic ordering, capacity planning, salience
preservation, coherence independence, digest tampering, explicit tombstone
semantics, atomic round-trip, invalid weights, invalid scores, and timezone
validation. This is a retention planner increment, not a completed Core-level
compaction/restore implementation.
