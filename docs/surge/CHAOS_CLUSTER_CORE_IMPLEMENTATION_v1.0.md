# Chaos Cluster Core Implementation v1.0

## Implemented in this increment

The persistent C++ `ChaosClusterLedger` now extends the v0.1 snapshot format compatibly:
- Existing base snapshot payloads remain readable; an optional `CC2` extension carries v1 organization state, creation provenance, association status history and PatternHypothesis records.
- Association proposals start as PROVISIONAL regardless of caller-supplied status. Review is append-only in the per-association history. REJECTED and EXPIRED are terminal states.
- Identical member and cluster-create retries are treated as duplicates; changed member payload under an occupied member identity is rejected.
- PatternHypothesis validates explicit member references and remains separate from Claim.
- Fork and split create child exploration spaces with parent/child lineage, copy only selected members and internal associations, and retain the parent snapshot and source events.
- Split subsets must be nonempty, disjoint and refer to existing members. Fork/split require REVIEWABLE source state and are committed as one journal transaction.
- Stale expected state versions are rejected by EventJournal; invalid lifecycle transitions fail before commit.

## Compatibility and limits

The C++ ledger API is persistent and replayable, but the Chaos Cluster operations are not yet exposed through the public C ABI/Python bridge. Score observations, diversity-aware retrieval, Question/Interest/Focus graph edges and full CC01-CC20 executable semantics remain follow-up work. The current serialization uses a backward-compatible optional extension marker, not a general-purpose public interchange encoding.

## Verification

The dedicated C++ test covers create/member retry, provisional association and terminal review history, PatternHypothesis, subset promotion, fork, split, restart replay, stale-version rejection and invalid lifecycle rejection. CI remains the authority for platform/compiler verification.
