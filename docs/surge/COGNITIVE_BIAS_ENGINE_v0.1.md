# SURGE Cognitive Bias Engine v0.1

**Status:** Initial deterministic Core policy implementation; not yet wired into atomic minimal-projection admission.  
**Normative parent:** `SPECIFICATION_v1.1.md` (v1.1.1 clarification)

## 1. Responsibility

The external Projector supplies only:
- Information;
- nearest existing Cluster ID or null;
- distance/score;
- metric profile and inventory revision.

It does not supply a classification label or Contains-edge weight. SURGE's own Cognitive Bias Engine derives those values from the projection and SURGE-owned cognition signals.

Cognitive classification is an internal organizing bias (for example ASSOCIATION, FOCUS, INTEREST, QUESTION_PRESSURE, NOVELTY, RECURRENCE). It is not an external semantic taxonomy such as DOG/PERSON/PLACE, and it is not a truth or confidence judgment.

## 2. Initial bias axes

| Axis | Source | Purpose |
|---|---|---|
| Association | Projection distance interpreted through its declared metric profile | Strength of the proposed contextual association |
| Focus | Current Core Focus state | Current attention alignment |
| Interest | Existing Core Interest state | Allocated attention to the target context |
| Question pressure | Unresolved Questions linked to the context | Unresolved cognitive demand |
| Novelty | Core's existing information/context inventory | Newness relative to current memory |
| Recurrence | Core's retained temporal history | Repeated activation, guarded against frequency-only importance |

These are Core-derived signals. They are not required Projector output. Each signal is normalized to [0,1] in the policy input. Unknown signals must eventually be represented explicitly as unknown; the initial C++ API currently accepts normalized numeric signals, so the admission integration must define missing-signal handling before production use.

## 3. Deterministic baseline policy

Policy v0.1 uses the following provisional coefficients:

| Axis | Coefficient |
|---|---:|
| Association | 0.35 |
| Focus | 0.20 |
| Interest | 0.15 |
| Question pressure | 0.15 |
| Novelty | 0.10 |
| Recurrence | 0.05 |

The coefficients are normalized by their sum. Membership weight is the weighted sum of available axis contributions, clamped to [0,1]. The dominant contribution selects the cognitive classification. If all contributions are zero, classification is UNRESOLVED.

These coefficients are a deterministic starting point, not calibrated psychological constants or universal importance weights. They must be evaluated on an independent, adjudicated corpus before being treated as production defaults.

## 4. Distance handling

Distance/score is never compared without its metric profile. For bounded profiles:
- lower-is-closer distance maps to affinity = 1 - normalized_distance;
- higher-is-closer similarity maps to affinity = normalized_similarity.

The Core rejects invalid bounds, non-finite values, out-of-range values, and distance supplied without a target. Null target contributes no association signal and proceeds through the unresolved/new-information bias path.

The engine does not compare scores across metric profiles and does not independently select another target.

## 5. Decision trace

A production admission must persist, in the same transaction as Information and membership:
- raw Projector distance and metric profile;
- Cognitive Bias policy ID/version;
- evaluated Core signals;
- per-axis contributions;
- selected cognitive classification;
- resulting membership weight;
- expected graph/inventory revision.

Replay must reproduce the same decision result. A policy upgrade affects only new decisions unless an explicit append-only reassessment is performed.

## 6. Implementation status

Implemented:
- deterministic C++ policy evaluator;
- profile-scoped distance normalization;
- bounded signal/weight validation;
- dominant-axis classification;
- per-axis contribution trace in the result;
- focused C++ conformance tests registered in CMake.

Not yet implemented:
- deriving all signals directly from ContextGraph state;
- explicit UNKNOWN signal semantics;
- atomic integration with MinimalProjectionAdmission + ContextGraph membership;
- journal serialization and replay of the decision trace;
- calibration corpus and threshold/weight evaluation.

Do not claim end-to-end Cognitive Bias admission until these remaining steps pass the Core transaction and replay gates.
