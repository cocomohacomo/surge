# SURGE Decision Policy Calibration v0.1

## Purpose

RecognitionDecision and NormalizationDecision remain Core-owned. Language/runtime
adapters may supply evidence, but evidence-family calibration is an explicit
policy input rather than a hidden application-side scoring branch.

## Policy contract

- `DecisionPolicy.family_weights` maps evidence family names to finite,
  non-negative multiplicative calibration values.
- An omitted family has multiplier 1.0.
- The policy map is bounded by `max_family_weights` (default 64); exceeding the cap defers the decision.
- A zero multiplier disables that family's score contribution without deleting
  its provenance or evidence trace.
- Invalid, negative, NaN, infinite, or overflowed contributions reject the
  decision rather than silently contaminating ranking.
- Existing per-evidence `weight` remains a local confidence/strength factor.
  Effective contribution is `value * evidence.weight * family_multiplier`.
- Correlation-group de-duplication is applied after calibration.
- Stable tie-breaking and bounded candidate/evidence counts remain unchanged.

## Ownership

SURGE Core owns policy application, validation, deterministic ranking, trace,
and disposition. A Language Set or an offline calibration procedure may
recommend policy values. Host applications must not implement a competing
candidate selector.

Normalization renderer output is a hypothesis, not an automatic command.
Its score is passed to NormalizationDecision as evidence. The Core may select
the source surface, select the rendered surface, or defer according to policy.

## Calibration protocol

1. Fit language resources and context models using train only.
2. Evaluate a fixed, bounded policy grid on dev only.
3. Select minimum CER, then maximum sentence exact, then fixed grid order.
4. Persist the chosen policy with pack/version, evaluator version, dataset
   source lock, split, grid definition and metrics.
5. Freeze the policy before evaluating test.
6. Never update weights from test results.

The first reference calibrator is deliberately a small deterministic preset
grid. It is a reproducible baseline, not a claim of globally optimal weights.
Later versions may use bounded coordinate search or a learned calibration
model, provided they preserve deterministic replay, provenance and resource
ceilings.

## Required conformance

- Default multiplier is 1.0.
- Zero multiplier changes ranking when that family was decisive.
- Negative/non-finite multipliers are rejected.
- Overflowed effective contributions are rejected.
- Missing family entries preserve legacy behavior.
- Test split cannot be used by the calibration command.
- Test evaluation records the exact policy used.
- Calibration must not change the language pack or train/dev/test split.
