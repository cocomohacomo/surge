# Chaos Cluster Model v0.1

## Purpose
A Chaos Cluster is a first-class cognitive holding area for information that cannot yet be organized into a stable topic, proposition, causal chain or temporal narrative. It deliberately permits apparent disorder. SURGE recognizes information; it must not force every event into a neat topic.

A Chaos Cluster is not a junk bin, error state, low-quality label or discard queue. Low coherence is not low value.

## Separate dimensions
- Organization: strength of topical or structural organization.
- Epistemic status: supported, contradicted, unverified, hypothetical or retracted.
- Salience: relevance, surprise, novelty and potential importance.
- Recognition confidence: confidence of a particular model and policy.
- Utility: possible value for later retrieval or creative synthesis.

Low organization must not imply low salience, falsehood, irrelevance or deletion. Missing scores mean unknown, not zero.

## Membership and associations
Membership is many-to-many. An event may remain in a Chaos Cluster while also linking to ordinary topic clusters. Original event, source range, timestamps and provenance remain immutable.

Associations are typed and provisional:
- CO_OCCURS: same stream/episode, no causal claim.
- ANALOGY: possible resemblance, not identity.
- HYPOTHESIS: candidate explanatory connection.
- QUESTION: retained because it raises an unresolved question.
- CONTRADICTION: incompatible readings retained together.
- TEMPORAL_NEARNESS: proximity, not causality.
- UNCLASSIFIED: co-membership only, no semantic relation asserted.

Every association records confidence, status (PROVISIONAL/REVIEWED/REJECTED), provenance and model/policy version. Rejected associations remain in history.

## Lifecycle
OPEN -> ACCUMULATING -> EMERGING -> REVIEWABLE -> PROMOTED.
Alternative transitions: FORKED, DORMANT, ARCHIVED. Promotion is optional and never deletes the source cluster. Promotion creates explicit links or a new ordinary cluster with decision provenance. New evidence may return an emerging cluster to accumulation. Split/fork preserves the original membership snapshot and child lineage.

## Recognition safeguards
1. Do not force a single topic when evidence is insufficient.
2. Preserve competing assignments and model disagreement.
3. Co-occurrence or analogy must not become SUPPORTS, CAUSES or factual DEPENDS_ON without separate evidence.
4. A high score alone must not convert a hypothesis into a Claim.
5. Raw events remain retrievable even when semantic scores are low.
6. Low coherence alone must never trigger pruning.
7. Exploratory Focus may target a Chaos Cluster without claiming all members share a subject.
8. Creative synthesis remains HYPOTHESIS or CREATIVE_SYNTHESIS until independently validated.

## Scoring profile
Optional [0,1] observations: topical, causal, temporal and evidential coherence; novelty, diversity, salience and ambiguity. Each score records scorer/model/policy version and observation time. Avoid collapsing these into one chaos score. A scalar ordering measure must not alone drive deletion, promotion, truth assessment or memory eviction.

## Memory pressure
Preserve source references and a compact membership manifest before evicting verbose projections. Retention uses explicit policy, salience, recency, retrieval history and provenance, never low coherence alone. Dormant clusters remain recoverable.

## Security
Model-generated associations are untrusted proposals. Validate references, provenance and size budgets before persistence. Proposals cannot mutate source records. Promotion, merge, split and rejection are journaled transitions.

## Conformance scenarios
- Short acknowledgement with unclear referent remains unclassified, not noise.
- Unrelated observations coexist without forced topic collapse.
- An analogy stays provisional and does not become evidence.
- Contradictory hypotheses remain visible together.
- New evidence promotes a subset while preserving original history.
- Low-coherence/high-salience fragments are not pruned.
- Model disagreement remains represented.
- Archived clusters can be retrieved and reactivated.

## Implementation status
This document and its descriptor schema define the v0.1 semantic contract. The current C++ ContextGraph does not yet persist a dedicated ChaosCluster node kind. The first step is an adapter-level descriptor boundary; graph persistence requires a separate versioned migration and conformance implementation.
