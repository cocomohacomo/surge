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

The C++ ledger API is persistent and replayable. The public C ABI and Python bridge currently expose create, member admission, provisional association, lifecycle transition, subset promotion and bounded readback. Fork/split, association review, PatternHypothesis creation, Focus routing is now implemented through a distinct `CHAOS_CLUSTER` graph node and `set_chaos_focus`; the target is never coerced to an ordinary `CLUSTER`. Score observations, diversity-aware retrieval and full CC01-CC20 executable semantics remain follow-up work. The current serialization uses a backward-compatible optional extension marker, not a general-purpose public interchange encoding.

## Verification

The dedicated C++ test covers create/member retry, provisional association and terminal review history, PatternHypothesis, subset promotion, fork, split, restart replay, stale-version rejection and invalid lifecycle rejection. The public C ABI is covered by `surge_chaos_c_api_tests`; the Python `SemanticCoreBridge` now exposes create, member admission, provisional association, lifecycle transition, subset promotion and bounded JSON readback and Chaos Cluster Focus routing. Its integration test exercises the complete C ABI-to-Python path and restart replay. CI remains the authority for platform/compiler verification.


## Bounded read projection and missing mutation adapters

The C ABI now exposes association review and PatternHypothesis creation in
addition to create/admit/propose operations. PatternHypothesis remains separate
from Claim and retains member, excluded-member, counterexample, and alternative
references with provenance.

`surge_chaos_cluster_get_bounded` exposes the authoritative ledger, including
association provenance and append-only status revision history, PatternHypothesis
records, lineage, and promotion targets. Each collection has an independent
caller budget: members, associations, hypotheses, history revisions per
association, and hypothesis references per vector. Budgets are validated before
reading and the serialized response is capped at 16 MiB.

The response includes `complete` and exact omission counters. History truncation
keeps the newest revisions and reports `history_offset`, so a truncated
history cannot be mistaken for a complete audit trail. Hypotheses remain
hypothetical records and are never promoted to Claims by retrieval.
