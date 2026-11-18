# Native Snapshot Domain Adapters v0.2

## Connected production domains

The snapshot coordinator is connected to four production domains:

- Evidence provenance preserves each evidence ID's ordered revision history, including observed, published, and retrieved timestamps and recorded state versions.
- Chaos clusters use the ledger's native versioned encoder and decoder, preserving lifecycle, membership, association, hypothesis, observation, and provenance history represented by that format.
- Temporal boundaries preserve per-claim candidate sets, valid-time boundaries, evidence/packet references, conflict state, selected candidate IDs, and selection watermarks.
- Temporal questions preserve question identity, claim/role, lifecycle status, last event ID, candidate references, evidence references, and packet references.

These are native ledger-state adapters, not synthetic test-only payloads.

## Restore contract

All adapters decode and validate detached candidate state during prepare_restore. The coordinator publishes only after every registered domain has prepared successfully. Publication uses preallocated map swaps. Chaos Cluster publication uses its ledger mutex; the application must still quiesce all readers and writers across the complete registered domain set.

Evidence provenance validates record identity, contiguous per-evidence revisions, increasing recorded state versions per evidence, RFC3339 timestamps, duplicate IDs, and bounded record counts. Chaos Cluster delegates to its native versioned decoder and verifies map keys match embedded cluster IDs. Temporal Boundary validates candidate roles, evidence and packet references, selection maps, selected flags, and configured claim capacity. Temporal Question validates canonical question IDs, lifecycle states, unique references, and configured question capacity.

## Consistency and concurrency

The application must quiesce all readers and writers of every registered domain at one common state boundary before capture and restore. The outer snapshot state version and journal sequence must describe that same boundary. The coordinator does not create a global lock or infer cross-ledger atomicity from individual mutexes.

## Remaining release gates

ContextGraph is not yet connected. EventJournal tail reading now exposes transaction state-version watermarks and a bounded read-only tail, but tail application to domain reducers and differential replay equivalence are not yet implemented. No journal truncation or compaction is authorized.
