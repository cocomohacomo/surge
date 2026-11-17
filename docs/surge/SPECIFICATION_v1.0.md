# SURGE 基礎設計 v1.0
## Universal Context Cognition Framework
### Open-World Recognition and Chaos Cluster Cognition

Version: 1.0.0
Status: Normative additive specification
Parent: SPECIFICATION_v0.9.md
Detailed Chaos Cluster model: CHAOS_CLUSTER_MODEL_v1.0.md
Schema: schemas/surge-chaos-cluster-v1.0.schema.json
Conformance: tests/conformance/SURGE_CHAOS_CLUSTER_v1.0.json

## 1. Version scope

v1.0 retains the v0.9 streaming, source-integrity, immutable Evidence, Claim lifecycle, temporal validity, Question/Interest/Focus, ContextGraph, transaction, replay and quality-gate contracts. It adds a normative open-world recognition model and first-class semantics for Chaos Clusters.

v1.0 does not imply that all new Chaos Cluster operations are already implemented in the C++ Core. The schema and conformance inventory define the target contract; implementation status must be reported separately.

## 2. Normative principle: recognition before organization

SURGE recognizes information; it does not require every recognized event to be immediately organized into one coherent topic. An input may be:
- recognized but not yet classified;
- associated with several candidate topics;
- apparently unrelated to current context;
- contradictory under competing interpretations;
- useful as a creative association without being evidence;
- important despite low topical coherence.

The absence of an established explanation is a valid cognitive state, not a recognition failure. SURGE MUST preserve the distinction between "not organized", "not understood", "false", "irrelevant" and "discarded".

## 3. First-class Chaos Cluster

A Chaos Cluster is a persistent, revisable, open-world context container for events whose semantic organization is unresolved, plural, exploratory or intentionally unconstrained. It is not a conventional topic cluster with a low score, a trash bin, a temporary error buffer or a synonym for noise.

A Chaos Cluster:
1. retains immutable source/event references and provenance;
2. permits many-to-many membership with ordinary topic clusters and other Chaos Clusters;
3. permits members with no asserted semantic relation to one another;
4. retains multiple competing interpretations and recognizer disagreement;
5. supports provisional associations and exploratory patterns;
6. supports partial promotion, fork and split without deleting the parent history;
7. remains retrievable while dormant or archived.

Membership means "retained in this exploration space", not "these items share one topic", "these items are true", or "these items are causally related".

## 4. Separate semantic dimensions

Implementations MUST NOT collapse the following dimensions into a single quality/chaos scalar:

| Dimension | Meaning | Must not be inferred from |
|---|---|---|
| Organization | Degree of stable topical/structural arrangement | Truth or utility |
| Epistemic status | Supported, contradicted, unverified, hypothetical, retracted | Model confidence alone |
| Salience | Potential importance, surprise, relevance, urgency | Coherence alone |
| Recognition confidence | Confidence of a named model/policy on a specific task | Epistemic truth |
| Novelty | Difference from currently represented patterns | Importance |
| Diversity | Breadth of distinct patterns represented | Disorder or error |
| Utility | Potential future retrieval, reasoning or synthesis value | Current topic assignment |
| Temporal status | Valid-time and observation-time relationship | Stream adjacency |

Each score is optional and provenance-bound to model, model version, policy version and observation time. Missing is UNKNOWN, not zero. Scores are observations, not facts or authorization.

## 5. Membership, associations and pattern hypotheses

### 5.1 Membership
A member record references an immutable Event/SourceRange and records membership status, admission reason, provenance and optional salience observation. Duplicate membership requests are idempotent by canonical member identity. Membership does not mutate the source event.

An event may simultaneously belong to multiple normal clusters and one or more Chaos Clusters. Removing a membership edge does not delete the event or its history.

### 5.2 Association vocabulary
Allowed association types:
- CO_OCCURS: shared episode/window; no causal assertion.
- ANALOGY: proposed resemblance; not identity or evidence.
- HYPOTHESIS: candidate explanatory relationship.
- QUESTION: association retained because it raises an unresolved question.
- CONTRADICTION: incompatible interpretations or claims.
- TEMPORAL_NEARNESS: temporal proximity only.
- UNCLASSIFIED: no semantic relation asserted.
- CREATIVE_SYNTHESIS: exploratory combination intended to generate a new candidate idea.

Every association has status PROVISIONAL, REVIEWED or REJECTED, confidence (if available), provenance and optional valid-time. Rejection is append-only and does not erase the proposal.

No association above may be silently converted into SUPPORTS, CAUSES or DEPENDS_ON. Such a typed relation requires an independent, validated SemanticDelta with Evidence and normal Core validation.

### 5.3 Pattern hypotheses
A PatternHypothesis is a candidate regularity across members, not a Claim. It records member refs, inclusion/exclusion rationale, counterexamples, alternative hypotheses, confidence observations and review status. It may be promoted to Claims only through ordinary Evidence-backed Claim creation. A pattern hypothesis MUST remain explicitly hypothetical until then.

## 6. Lifecycle and transitions

Lifecycle:
- OPEN: created, may be empty.
- ACCUMULATING: events are admitted without requiring a stable organizing pattern.
- EMERGING: one or more candidate patterns are detectable but remain provisional.
- REVIEWABLE: a bounded subset or pattern is ready for explicit review.
- PROMOTED: at least one subset has been promoted; residual membership may remain unresolved.
- FORKED: a child exploration space has been created with explicit parent lineage.
- DORMANT: not currently focused; fully recoverable.
- ARCHIVED: excluded from default active retrieval but retained under retention policy.

Permitted high-level transitions:
- OPEN -> ACCUMULATING
- ACCUMULATING -> EMERGING | REVIEWABLE | DORMANT
- EMERGING -> ACCUMULATING | REVIEWABLE | DORMANT
- REVIEWABLE -> ACCUMULATING | PROMOTED | FORKED | DORMANT
- PROMOTED -> ACCUMULATING | DORMANT | ARCHIVED
- FORKED -> ACCUMULATING | DORMANT | ARCHIVED
- DORMANT -> ACCUMULATING | EMERGING | REVIEWABLE | ARCHIVED
- ARCHIVED -> DORMANT only by explicit audited reactivation

A transition outside this table is rejected. Reassessment may move EMERGING back to ACCUMULATING when a candidate pattern loses support. Promotion is never mandatory.

## 7. Promotion, fork and split

Promotion is subset-first. The operation MUST identify exact member refs, target cluster kind/identity, policy version, decision provenance and source state version. Unselected members remain in the original Chaos Cluster. Promotion creates lineage edges and does not rewrite source membership history.

Fork creates a child Chaos Cluster with a snapshot of selected members and explicit parent/child lineage. Split partitions selected membership into child clusters; the parent remains queryable and its historical membership snapshot remains immutable. Merge of Chaos Clusters is a new journaled operation with explicit source refs and conflict policy, never destructive identity replacement.

A promotion/fork/split operation MUST be atomic with its journal event and projection update. Stale state versions fail with a conflict and require re-evaluation.

## 8. Question, Interest and Focus integration

A contradiction or unresolved pattern may create or update a Question. The Question points to the exact conflicting members/associations and keeps competing interpretations. Interest may target a Question, a member, a PatternHypothesis or an entire Chaos Cluster. Focus may target a Chaos Cluster as an exploration region without asserting that all members share one subject.

Focus changes retrieval priority, not truth status. A Focus on a Chaos Cluster MUST allow diversity-aware retrieval and MUST NOT automatically collapse the cluster into one topic. Resolving a Question does not automatically promote or delete its parent Chaos Cluster.

## 9. Retrieval and exploration

Retrieval supports at least:
- exact member/source lookup;
- provenance and temporal filtering;
- association-type/status filtering;
- competing-interpretation retrieval;
- novelty/diversity-oriented sampling;
- salience-oriented retrieval;
- pattern hypothesis and counterexample retrieval;
- parent/child lineage traversal.

Default retrieval MUST distinguish direct membership from inferred association. Provisional and rejected associations are returned with their status, never as established facts. Ranking may combine task-specific factors, but no single score may hide uncertainty or suppress all low-coherence members.

An exploration result records query, state version, policy/model versions, included and omitted refs, ranking rationale and completeness. Stale snapshots are re-evaluated or marked stale.

## 10. Memory, forgetting and retention

Memory pressure may compact projections and indexes, but source identity, provenance, membership lineage and a recoverable manifest MUST remain according to retention policy. Low organization/coherence alone MUST NOT be a deletion or eviction criterion.

Retention may consider salience, unresolved Question linkage, Focus relevance, recurrence, recency, novelty, diversity, retrieval history, provenance importance and storage cost. Factors and policy version must be inspectable. A dormant/archived object is not forgotten semantically; it is merely deprioritized.

If source payload deletion is required by an external retention/privacy policy, SURGE records a tombstone and the permitted non-content audit metadata. It must not pretend the deleted payload remains retrievable.

## 11. Streaming and temporal behavior

Chaos Cluster admission is incremental and input-granularity independent. Event arrival order is preserved by the v0.9 source sequencer. A late event may join an existing cluster or reopen exploration, but cannot rewrite prior decisions. Every association and score is timestamped; temporal proximity is not causality.

Time-sensitive patterns may have valid-time windows and expiry. Expiry changes the current applicability of an association, not its historical existence. Reassessment is append-only and version-pinned.

## 12. Safety and integrity invariants

- CC-I01: Membership never asserts semantic equivalence.
- CC-I02: Low coherence never implies low value, falsehood or deletion.
- CC-I03: Source/Event/Evidence records are immutable.
- CC-I04: Membership is many-to-many and may be removed only as an append-only edge revision.
- CC-I05: Association proposals are untrusted and provenance-bound.
- CC-I06: CO_OCCURS, ANALOGY, TEMPORAL_NEARNESS and CREATIVE_SYNTHESIS never auto-create factual relations.
- CC-I07: PatternHypothesis is not a Claim.
- CC-I08: Competing interpretations and model disagreement remain retrievable.
- CC-I09: Promotion/fork/split is subset-explicit, atomic and lineage-preserving.
- CC-I10: Stale state versions cannot be committed.
- CC-I11: Missing score is UNKNOWN, never zero.
- CC-I12: No scalar score alone authorizes promotion, truth assignment or eviction.
- CC-I13: Dormant/archived clusters remain recoverable subject to retention policy.
- CC-I14: Retrieval distinguishes direct membership, provisional association and validated relation.
- CC-I15: Rejection and expiry preserve history.
- CC-I16: Question resolution does not implicitly resolve cluster organization.

## 13. Conformance

The v1.0 suite defines 20 semantic cases CC01-CC20. These are design/conformance expectations, not proof of executable Core behavior. Schema validation, fixture structural validation, Core unit/integration tests, restart replay, concurrency tests and model-quality evaluation are separate evidence classes.

A v1.0 implementation may claim:
- schema conformance only after Draft 2020-12 validation;
- fixture inventory conformance only after all case assertions execute;
- Core conformance only after transaction, replay and failure-path tests;
- performance conformance only after measured benchmark results.

## 14. Migration and compatibility

v0.1 Chaos descriptors may be imported through an explicit adapter. Missing fields are UNKNOWN or LEGACY_UNSPECIFIED; they are not inferred as false, zero, reviewed or supported. Existing normal Cluster records are not automatically converted to Chaos Clusters. Migration preserves source refs and records the migration policy/version.

## 15. Implementation boundary

As of the 2026-11-18 implementation audit, the repository has a typed persistent ChaosCluster ledger, journal replay, C++/C ABI/Python operations, Focus routing, bounded JSON projection, scorer-bound cluster/member/association/hypothesis observations, membership revisions, association valid-time/expiry, PatternHypothesis review history, lifecycle history, fork/split and atomic merge. These are implementation increments, not a declaration of full v1.0 conformance.

Remaining conformance work includes exhaustive executable CC01-CC20 assertions and adversarial migration/replay cases; complete query filters and multi-hop lineage retrieval; full retrieval ranking across all task-specific score dimensions; memory compaction/manifests/tombstones; multi-process and crash/power-loss testing; fuzz/sanitizer coverage; and measured performance on the target 30 MiB model and hardware. Schema conformance, Core behavior, security hardening and performance evidence remain separate claims.
