# Native Snapshot Domain Adapters v0.1

This increment connects the snapshot coordinator to two production domains.

- Evidence provenance preserves each evidence ID's ordered revision history, including observed, published, and retrieved timestamps and recorded state versions.
- Chaos clusters use the ledger's native versioned encoder and decoder, preserving lifecycle, membership, association, hypothesis, observation, and provenance history represented by that format.

Both adapters decode and validate detached candidate state during prepare_restore. The coordinator publishes only after every registered domain has prepared successfully. Evidence history is published by map swap. Chaos state is published by a mutex-protected map swap.

The evidence adapter validates record identity, contiguous per-evidence revisions, increasing recorded state versions per evidence, RFC3339 timestamps, duplicate IDs, and bounded record counts. The chaos adapter uses the native decoder and verifies map keys match embedded cluster IDs.

The application must quiesce all readers and writers of every registered domain at one common state boundary before capture and restore. The outer state version and journal sequence must describe that same boundary.

These adapters do not restore or truncate EventJournal, replay a post-snapshot tail, migrate schemas, or authorize compaction. ContextGraph and temporal ledgers are not yet connected. Differential replay across all production domains remains a release gate.
