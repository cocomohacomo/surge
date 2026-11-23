# SURGE Decision Policy Calibration v0.1

## Purpose

Recognition and surface normalization are one Core-owned UnifiedDecision. Language/runtime
adapters may supply interpretation and rendering hypotheses, but they cannot run
separate selectors. Evidence-family calibration is an explicit policy input rather
than a hidden application-side scoring branch.

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
surface selection and final disposition. UnifiedDecision first ranks the
interpretation candidates, then evaluates the selected candidate's surface
hypotheses under the same policy and within the same decision call. A failed
surface decision invalidates the unified result and clears all selected surfaces.
A Language Set or offline calibration procedure may recommend policy values.
Host applications must not implement a competing candidate or surface selector.

Normalization renderer output is a hypothesis, not an automatic command.
Its score is passed to UnifiedDecision as evidence alongside the original
source surface. The Core may select the source surface, select the rendered
surface, or defer the entire unified decision.

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

- Recognition and surface choices are returned by one UnifiedDecision call.
- Every surface set is uniquely keyed by source span/reference and has at least one hypothesis.
- A failed surface decision clears all selected surfaces and does not expose a partial selection.
- Default multiplier is 1.0.
- Zero multiplier changes ranking when that family was decisive.
- Negative/non-finite multipliers are rejected.
- Overflowed effective contributions are rejected.
- Missing family entries preserve legacy behavior.
- Test split cannot be used by the calibration command.
- Test evaluation records the exact policy used.
- Calibration must not change the language pack or train/dev/test split.
