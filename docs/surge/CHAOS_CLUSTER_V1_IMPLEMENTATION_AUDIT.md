# Chaos Cluster v1.0 implementation audit

Audit baseline: main `06daf8ac411d861539e9d65f3ee3a9b6090b989c` plus
the Focus retrieval integration merged immediately afterward.
Normative references: `SPECIFICATION_v1.0.md`,
`CHAOS_CLUSTER_MODEL_v1.0.md`, and
`schemas/surge-chaos-cluster-v1.0.schema.json`.

This is a source-level implementation audit, not a security certification or
independent semantic-quality evaluation.

## Status definitions

- **Implemented**: native code, exposed API where applicable, and regression
  coverage exist.
- **Partial**: a meaningful subset exists, but one or more normative contracts
  or public interfaces are missing.
- **Missing**: no corresponding implementation was found in the audited path.
- **Unverified**: source suggests behavior, but required failure/concurrency/
  durability evidence has not been run.

## Findings

| Priority | Requirement | Status | Gap |
|---|---|---|---|
| P0 | Orthogonal score observations | Partial | Cluster, Member Salience, Association Confidence and PatternHypothesis Confidence now persist as scorer-bound append-only histories and are exposed through C ABI/Python bounded projections. Score-aware retrieval is missing. |
| P0 | Membership status and unmembership | Partial | Native C++ ledger, CC4 replay, C ABI/Python revision endpoint, bounded JSON history and replay tests are implemented; exhaustive lifecycle/legacy-fixture tests remain. |
| P1 | PatternHypothesis auditability | Partial | Exclusion reasons, append-only review revisions and confidence observations now persist and are exposed in bounded JSON; negative/terminal transition and migration matrix still needs expansion. |
| P1 | Association valid-time | Partial | UTC valid_from/valid_until persist in CC6; temporal C ABI/Python proposal and explicit as-of expiry transaction append EXPIRED provenance revisions. Automatic scheduling is intentionally external to Core. |
| P1 | Retrieval diversity | Partial | Focus retrieval round-robins admission-reason groups. It does not rank or constrain by persisted novelty, salience, diversity, ambiguity or utility observations. |
| P1 | Retrieval query dimensions | Partial | Bounded full-cluster projection and typed Focus retrieval exist. Dedicated filtering by association type/status, temporal range, provenance, hypotheses/counterexamples and lineage traversal is incomplete. |
| P1 | Lifecycle and operations | Partial | Transition table includes ARCHIVED -> DORMANT. Dedicated audited reactivation, merge, and lifecycle transition history as first-class records are missing. Fork/split/subset promotion exist. |
| P1 | CC01-CC20 executable conformance | Partial | C++/C ABI/Python tests cover selected persistence, replay, lifecycle, association, hypothesis, promotion/fork/split and Focus paths. The complete 20-case semantic matrix is not yet executed end-to-end. |
| P2 | Memory and forgetting | Partial/Missing | Lifecycle dormancy/archive exists, but manifest compaction, inspectable eviction factors, recoverable archive workflow, payload tombstone policy and restore drill are not complete. |
| P2 | Operational safety | Unverified | Multi-process writer coordination, concurrent Core-handle mutation contract, hostile-input fuzzing, sanitizers, crash/power-loss and backup/restore drills remain outstanding. |
| P2 | Performance | Unverified | No declared-hardware measurement proves 30 MiB model integration, RTF <0.01, p99 latency or peak RSS for the v1.0 path. |

## Implementation order

1. Extend observation ownership to member salience, association confidence and
   PatternHypothesis confidence, preserving independent scorer lineage.
2. Add member membership status and append-only unmembership/reinstatement.
3. Add PatternHypothesis review/exclusion rationale and association valid-time.
4. Implement score-aware retrieval with independently configurable novelty,
   salience and diversity constraints, explicit ranking rationale and omissions.
5. Close all CC01-CC20 assertions, including negative and restart cases.
6. Complete merge/reactivation, retention manifests/tombstones, and operational
   hardening before production claims.

## First implementation tranche

The score-observation persistence work is being delivered separately from this
audit. It is intentionally limited to the native C++ ledger first. It does not
claim full score observation conformance until the C ABI/Python adapters,
bounded projection and score-aware retrieval are implemented and tested.


## Follow-up: score observation public API

The cluster-level score observation operation is now exposed through the C ABI
and Python `SemanticCoreBridge.record_chaos_score_observation`. Bounded JSON
returns the latest value in the schema-compatible `observations` object and
history revisions in `observation_history`, with offsets and omission counts.
Validation includes finite [0,1] values, required scorer/policy/timestamp
metadata, dimension allow-list, response cap, and restart replay tests.


## Follow-up: membership lifecycle

Member membership status and active/inactive state are now append-only,
version-checked journal mutations. The CC4 extension is optional and legacy
snapshots remain readable with the default active/UNCLASSIFIED state. The C ABI
and Python bridge expose removal and reinstatement; bounded projection reports
membership-history offsets and omission counts. Full adversarial and migration
coverage remains part of conformance closure.


## Follow-up: entity-scoped score observations

The CC5 extension adds independent member Salience, association Confidence,
and PatternHypothesis Confidence histories. A generic entity observation write
is available through C ABI and Python. Bounded projections expose latest
schema-compatible observations, history offsets, and omission counts. Replay
and API tests cover these paths; score-aware retrieval remains outstanding.


## Follow-up: association validity

Association validity bounds are persisted in CC6 and projected in bounded JSON.
The original C ABI input layout remains unchanged; temporal proposals use a
separate versioned input structure. A caller-supplied UTC as-of timestamp drives
an atomic expiry transaction, with append-only status/provenance history. The
Core does not consult a hidden wall clock; scheduling and invocation remain
host responsibilities.


## Follow-up: PatternHypothesis auditability

CC7 persists review status, epistemic-state revisions, per-member exclusion
rationales, and provenance. C ABI/Python review operations enforce explicit
excluded-member references and terminal review constraints. Bounded projection
includes capped revision history and omission metadata.


## Follow-up: lifecycle and merge

CC8 persists lifecycle transitions with reason and provenance. Merge is a
single version-checked transaction that archives sources, links lineage, and
preserves their members, associations, hypotheses and score histories.
Conflicting identities or divergent lifecycle-sensitive state reject the
transaction. The C ABI and Python bridge expose merge. Reactivation remains an
explicit audited lifecycle transition from ARCHIVED to DORMANT.
