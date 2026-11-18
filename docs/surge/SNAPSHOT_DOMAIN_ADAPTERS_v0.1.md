# Native Snapshot Domain Adapters v0.3

## Connected production domains

The snapshot coordinator is connected to five production domains:

- ContextGraph preserves nodes, relations, scope-span proposals and validation outcomes, focus and focus history, question-aspect status, question memory state/reference counts, claim lifecycle, claim/evidence validity histories, dependency evaluations, temporal reassessment chains, and delta idempotency fingerprints.
- Evidence provenance preserves each evidence ID's ordered revision history, including observed, published, and retrieved timestamps and recorded state versions.
- Chaos clusters use the ledger's native versioned encoder and decoder, preserving lifecycle, membership, association, hypothesis, observation, and provenance history represented by that format.
- Temporal boundaries preserve per-claim candidate sets, valid-time boundaries, evidence/packet references, conflict state, selected candidate IDs, and selection watermarks.
- Temporal questions preserve question identity, claim/role, lifecycle status, last event ID, candidate references, evidence references, and packet references.

## Restore contract

All adapters decode and validate detached candidate state during prepare_restore. The coordinator publishes only after every registered domain has prepared successfully. Publication uses preallocated map swaps. Chaos Cluster publication uses its ledger mutex; the application must still quiesce all readers and writers across the complete registered domain set.

ContextGraph restoration rebuilds node and relation invariants through the graph's native apply validators, validates scope anchor identity and graph references, restores temporal validity records through their native validators, replays dependency evaluation records through the native event validator, and verifies reassessment hash chains against the reconstructed claim/evidence state. It also restores delta idempotency fingerprints so already-applied semantic deltas are not accidentally admitted again.

ScopeSpan records retain their proposal, anchors, validation status/effect, and unconditional-claim flag. ContextGraph's current live state does not retain the original full source snapshot, so the adapter cannot rerun the complete source-text validator during restore. It validates the persisted anchor metadata and referenced graph nodes instead. This limitation is explicit and does not silently fabricate source text.

Evidence provenance validates record identity, contiguous per-evidence revisions, increasing recorded state versions per evidence, RFC3339 timestamps, duplicate IDs, and bounded record counts. Chaos Cluster delegates to its native versioned decoder and verifies map keys match embedded cluster IDs. Temporal Boundary validates candidate roles, evidence and packet references, selection maps, selected flags, and configured claim capacity. Temporal Question validates canonical question IDs, lifecycle states, unique references, and configured question capacity.

## Consistency and concurrency

The application must quiesce all readers and writers of every registered domain at one common state boundary before capture and restore. The outer snapshot state version and journal sequence must describe that same boundary. The coordinator does not create a global lock or infer cross-ledger atomicity from individual mutexes.

## Remaining release gates

EventJournal tail reading exposes transaction state-version watermarks and a bounded read-only tail. Applying that tail to all domain reducers, differential replay equivalence, crash-consistent snapshot file persistence, and atomic copy-on-write compaction remain separate release gates. No journal truncation or compaction is authorized.
