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


## Score observation persistence increment

The C++ ledger now persists cluster-level orthogonal score observations in the
backward-compatible optional `CC3` snapshot extension. Supported dimensions
are `organization`, `salience`, `recognition_confidence`, `novelty`,
`diversity`, `ambiguity`, and `utility`. Every observation carries its
numeric value, scorer ID/version, policy version, and observation timestamp.
Values must be finite and within [0,1]. Missing dimensions remain absent
(UNKNOWN), never synthesized as zero.

Observations are append-only per dimension. Repeating the exact latest
observation is idempotent; changing the value or timestamp creates a new
revision. The CC3 extension is optional on read, so v0.1 and CC2 snapshots
remain readable. The observation write operation is now exposed through the C ABI and
`SemanticCoreBridge.record_chaos_score_observation`. The bounded JSON projection
returns the latest observation in the schema-compatible `observations` object
and append-only revisions in `observation_history`, with history offsets and
omission counts. This increment still records cluster-level observations only;
member salience, association confidence, hypothesis confidence and score-aware
retrieval remain open.


## Membership status and unmembership increment

Members now carry an explicit `membership_status` and `membership_active`
state plus append-only membership revisions. The native ledger supports
status review, unmembership, and reinstatement as version-checked journal
transactions. The optional `CC4` snapshot extension preserves this state
without changing the v0.1, CC2, or CC3 payload layouts. Bounded JSON emits the
current membership state and a capped revision history with an offset; omitted
membership revisions contribute to the response completeness flag and exact
omission count. The C ABI and Python bridge expose the revision operation.
Legacy snapshots without CC4 default to active UNCLASSIFIED membership with
no synthesized history.


## Entity-scoped orthogonal observations

The optional `CC5` snapshot extension now stores score observations on
individual members (Salience), associations (Confidence), and PatternHypotheses
(Confidence), independently of cluster-level dimensions. All observations
retain scorer identity/version, policy version, timestamp, and value, and are
append-only with bounded histories. The generic C++ ledger operation is
exposed as `surge_chaos_cluster_record_entity_observation` and through
`SemanticCoreBridge.record_chaos_entity_observation`. The bounded JSON
projection emits the latest schema observation plus a separate history object
and includes omitted entity revisions in completeness metadata. Legacy
snapshots without CC5 remain readable.


## Association valid-time and expiry

Associations now persist optional UTC validity bounds (`valid_from`,
`valid_until`) in the backward-compatible optional `CC6` extension. Bounds
use canonical `YYYY-MM-DDTHH:MM:SSZ` values and must be ordered when both are
present. The C ABI preserves the original association input layout and adds a
separate temporal proposal entry point. Python exposes temporal proposals and
an explicit, caller-clock-driven `expire_due_chaos_associations` transaction.
Due nonterminal associations transition to EXPIRED with an appended provenance
revision. Expiry is deterministic for a supplied `as_of_utc`; no hidden system
clock is read by the Core.


## PatternHypothesis review and exclusion rationale

The optional `CC7` extension stores append-only PatternHypothesis review
revisions, epistemic-state changes, per-member exclusion reasons, and provenance.
Creation records the initial review state. The review API validates status and
epistemic-state vocabularies, requires every exclusion rationale to refer to an
explicitly excluded member, and preserves terminal rejection/promotion states.
The C ABI and Python bridge expose the operation. Bounded JSON returns the
current rationale and a capped review history with offsets and omission counts.
