# SURGE Recognition Scoring v0.1

## Status and scope
Additive v0.9 execution contract. This defines a replaceable, untrusted scoring
boundary. It does not change Core commit authority or claim calibrated
probabilities or semantic accuracy.

## Feedback from 50 manually scored Japanese cases
1. Importance and confidence are different axes. Consequential statements may
   be uncertain; filler may be recognized confidently.
2. Short acknowledgements are context-dependent. Length and token count do not
   determine importance.
3. Negation, condition, modality, concession, quotation and temporal scope
   remain explicit typed cues. A single polarity label is insufficient.
4. Contradictory claims are both retained. Contradiction creates a Question
   candidate and routes Interest; it does not choose a winner.
5. Ambiguous references abstain or remain unresolved; they must not trigger
   guessed Focus or Entity merge.
6. Corrections are append-only revisions.
7. Repetition and filler frequency do not imply semantic importance.
8. Tabular/list structure and attribution are first-class cues.

## Score vector
All dimensions are in [0,1]: relevance (active-context relation), importance
(semantic consequence), relation_strength (links to claims/evidence/clusters),
confidence (certainty of interpretation), ambiguity (unresolved ambiguity,
higher means more ambiguous), and novelty (new information).

The vector is not collapsed into one weighted score. v0.1 priority_score is
exactly the explicit importance dimension and is only a review priority.
A composite formula is prohibited until an immutable benchmark profile,
per-label calibration and selective-risk evaluation justify it.

## Decision policy
- ACCEPT: confidence >= 0.85 and ambiguity <= 0.60.
- REVIEW: confidence >= 0.70 and ambiguity <= 0.60, but below ACCEPT confidence.
- ABSTAIN: confidence < 0.70 or ambiguity > 0.60.
ABSTAIN is not a negative label or zero importance. It prohibits treating the
candidate as a resolved semantic assertion. All scores are untrusted engine
outputs; Core validates references, scopes, causality, invariants and version
before commit.

## Execution contract
RecognitionScorer validates dimensions and returns a review decision. It has
no Core handle and cannot commit. The 50-case corpus is a manually scored,
deterministic regression set, not a representative gold-standard corpus or
calibrated quality benchmark.

## Next-stage gates
1. Build ClassificationProposal from typed candidates and score decisions.
2. Validate schema, packet lineage and state version before Core integration.
3. Add semantic E2E tests for scope, contradiction, revision, quotation,
   tabular/list structure and ambiguity abstention.
4. Establish a separately versioned calibration benchmark before tuning
   thresholds or any composite score.
