# Chaos Cluster Model v1.0
## Normative cognitive and data model

This document defines the v1.0 Chaos Cluster contract. It supersedes the v0.1 descriptor semantics while retaining an explicit migration path.

## 1. Cognitive definition

A Chaos Cluster is an open-world exploration space that preserves events whose stable organization is unresolved, plural, provisional or intentionally unconstrained. The key concept is not randomness. It is the system's ability to tolerate unresolved order without prematurely inventing order.

A cluster may contain:
- unrelated observations retained because they occurred in one stream;
- weakly associated fragments;
- high-salience fragments with no current explanation;
- multiple candidate topic assignments;
- contradictory hypotheses;
- analogies and creative combinations;
- fragments that may become meaningful only after future events.

Membership is a retention/inspection relation only. It is not a semantic assertion.

## 2. Boundary with ordinary clusters

| Property | Ordinary topic cluster | Chaos Cluster |
|---|---|---|
| Primary purpose | Represent a sufficiently stable context/topic | Preserve unresolved or plural organization |
| Membership meaning | Candidate topical relation under normal cluster policy | Retained in same exploration space, no shared-topic claim |
| Organization | Usually has an organizing center | May be unordered or multi-pattern |
| Interpretations | May converge on a dominant interpretation | Competing interpretations are first-class |
| Retrieval | Topic/relation relevance | Diversity, novelty, salience, unresolved links and provenance |
| Promotion | Not applicable | Optional, subset-first, lineage-preserving |

A member may belong to both kinds at once. A Chaos Cluster must not become a mandatory staging step for every event.

## 3. Orthogonal state model

Do not represent Chaos Cluster with a single chaos_score. Store independent optional observations:
- organization: UNORDERED, PROVISIONAL, MULTI_PATTERN, PARTIALLY_STRUCTURED;
- epistemic status: SUPPORTED, CONTRADICTED, UNVERIFIED, HYPOTHETICAL, RETRACTED, UNKNOWN;
- salience, recognition confidence, novelty, diversity, ambiguity and utility;
- temporal applicability and review state.

Each numeric observation is in [0,1], identifies model/scorer, model version, policy version and observed_at. Omission means UNKNOWN. Scores are not calibrated across different scorers unless a calibration profile explicitly says so.

## 4. Membership and provenance

Membership references immutable Event/SourceRange identity, not copied source text. Required identity is stable member_ref plus event_ref. Optional source digest must match the referenced immutable range. Membership carries admission_reason, status, admitted_at and producer provenance.

Many-to-many membership is permitted. A member can be in several ordinary clusters and multiple Chaos Clusters. Unmembership is a journaled edge revision; source event and previous membership history remain intact.

## 5. Association semantics

Types: CO_OCCURS, ANALOGY, HYPOTHESIS, QUESTION, CONTRADICTION, TEMPORAL_NEARNESS, UNCLASSIFIED, CREATIVE_SYNTHESIS.

Statuses: PROVISIONAL, REVIEWED, REJECTED, EXPIRED.

Association endpoints must resolve to members or typed graph objects. Self-links are rejected unless the association type explicitly supports reflexivity. Duplicate canonical associations are idempotent. Conflicting payload under an occupied association ID is an identity conflict, not an update.

Every association includes provenance and policy/model lineage. Rejected/expired edges remain queryable. The association vocabulary does not replace the normal Relation vocabulary. A conversion to SUPPORTS/CAUSES/DEPENDS_ON requires separate Evidence-backed validation.

## 6. PatternHypothesis

A PatternHypothesis records a candidate pattern across an explicit member set. It includes:
- hypothesis_id and descriptive statement;
- included member refs;
- excluded member refs and exclusion reasons;
- supporting candidate associations;
- counterexample refs;
- alternative hypothesis refs;
- epistemic state and confidence observations;
- producer and policy provenance;
- lifecycle and review history.

It is never a Claim. Claim creation requires normal Evidence/Claim validation and transaction. A pattern may be rejected while its member events remain available.

## 7. Lifecycle state machine

States and allowed transitions are normative in SPECIFICATION_v1.0.md §6. Invalid transitions fail closed. Transition events are append-only and include actor/producer, reason, policy version, timestamp, expected state version and resulting state version.

PROMOTED does not mean every member was promoted. It means one or more explicitly identified subsets were promoted. Residual unresolved members remain in the Chaos Cluster.

## 8. Operations and atomicity

Required logical operations:
- create_chaos_cluster
- admit_chaos_member
- propose_chaos_association
- review_chaos_association
- reject_chaos_association
- record_pattern_hypothesis
- transition_chaos_lifecycle
- promote_chaos_subset
- fork_chaos_cluster
- split_chaos_cluster
- reactivate_chaos_cluster
- retrieve_chaos_members

All mutating operations require expected_state_version, idempotency identity, provenance and policy version. Core validates references and lifecycle transition, then commits the event and projection atomically. A retry with identical canonical content is idempotent. Same operation identity with different content is rejected. A stale expected_state_version returns CONFLICT and requires read/re-evaluate/retry.

## 9. Question, Interest and Focus

Questions may refer to contradiction edges or competing hypotheses. Interest may target any cluster/member/hypothesis/question. Focus may point to the whole Chaos Cluster as an exploration target, without claiming one shared subject. Focus influences retrieval priority only. Resolving a Question does not close the Chaos Cluster automatically.

## 10. Retrieval contract

Retrieval result must preserve edge class:
- DIRECT_MEMBER
- PROVISIONAL_ASSOCIATION
- REVIEWED_ASSOCIATION
- VALIDATED_CORE_RELATION
- LINEAGE_REFERENCE

Never flatten these into one undifferentiated relevance list. Return provenance, confidence source, state version and completeness. A diversity/novelty retrieval policy should avoid selecting only near-duplicate high-score members.

## 11. Memory and forgetting

Low coherence is prohibited as a sole eviction criterion. Memory pressure first compacts derived text/index projections and retains a verifiable manifest of source/member identities, provenance and lineage. Eviction/dormancy decisions expose factors, policy version and reason. ARCHIVED is recoverable; payload deletion under external policy uses explicit tombstone semantics.

## 12. Threats and failure modes

- Premature topic collapse: prevent by allowing unclassified/multi-membership.
- Association hallucination: keep proposals provisional and provenance-bound.
- Analogy mistaken for evidence: separate vocabularies and require normal Evidence.
- Salience lost under low coherence: independent salience and retention factors.
- Cluster becomes an infinite junk drawer: support bounded subspaces, fork/split and explicit review, without forced promotion.
- Model disagreement erased: retain per-producer interpretations.
- Stale promotion: expected state version and atomic commit.
- Retrieval tunnel vision: diversity-aware retrieval and explicit omitted/completeness metadata.
- False certainty from scores: unknown handling and scorer provenance.
- Memory pressure destroys novelty: preserve manifest and prohibit coherence-only eviction.

## 13. Minimum conformance matrix

CC01 unclassified member retained.
CC02 co-membership does not imply relation.
CC03 analogy stays provisional.
CC04 contradictory interpretations coexist.
CC05 low coherence/high salience retained.
CC06 model disagreement preserved.
CC07 subset promotion leaves residual members.
CC08 fork preserves parent lineage.
CC09 split preserves source identity.
CC10 stale promotion rejected.
CC11 duplicate operation is idempotent.
CC12 reused operation ID with changed payload rejected.
CC13 invalid lifecycle transition rejected.
CC14 pattern hypothesis is not Claim.
CC15 Question resolution does not auto-promote.
CC16 Focus targets exploration space without topic collapse.
CC17 diversity retrieval includes lower-ranked distinct members.
CC18 rejected association remains auditable.
CC19 archived cluster can be reactivated.
CC20 memory compaction preserves manifest and provenance.

## 14. Implementation status

The v1.0 document, schema and fixtures define the normative target. They do not assert that the dedicated C++ persistence/API is complete. Core implementation and executable conformance are tracked separately.


## 15. Implementation status snapshot (2026-11-18)

The repository implementation now includes:
- persistent typed Chaos Cluster ledger with backward-compatible CC2-CC8 snapshot extensions;
- C++ Core, C ABI and Python bridge for creation, membership, association review,
  temporal expiry, score observations, PatternHypothesis review, lifecycle,
  promotion, fork/split and atomic merge;
- append-only score histories for cluster dimensions, member Salience,
  association Confidence and PatternHypothesis Confidence;
- append-only membership, association, hypothesis and lifecycle histories;
- bounded JSON projection with offsets, completeness and omission counters;
- typed Focus retrieval with Salience-aware ordering, admission-reason diversity,
  explicit selection rationale and inactive-member exclusion.

This is not yet a full v1.0 conformance claim. Remaining work includes the
complete CC01-CC20 semantic matrix and hostile-input/replay cases, all requested
retrieval filters and multi-hop lineage traversal, broader score-aware ranking,
memory compaction/manifests/tombstones, multi-process/crash/power-loss testing,
fuzzing/sanitizers, and target-hardware performance measurements. The audit
document is authoritative for the current gap list.
