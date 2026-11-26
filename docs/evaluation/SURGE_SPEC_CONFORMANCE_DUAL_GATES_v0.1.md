# SURGE Specification Conformance and Dual Quality Gates v0.1

Status: implementation plan / conformance contract (not a claim that gates are already wired into CI)
Target: SPECIFICATION_v1.0.md and additive v1.0.1 cluster-native memory clarifications
Tracks: Core Integrity and Recognition Quality, independently required

## 1. Final gate

A release / qualification result is PASS only when both tracks pass against the same commit, profile, test corpus revision and environment declaration. A skipped, missing, stale, or not-applicable-without-approved-reason track is NOT PASS.

```
Core Integrity PASS  AND  Recognition Quality PASS
                       |
                 SURGE E2E PASS
```

CI execution success is not semantic quality. A benchmark runner exit code alone cannot mark Recognition Quality PASS.

## 2. Core Integrity track

Required checks:

- Admission coverage: every public ingress route (C++, C ABI, Python, adapter/runtime) reaches the same admission/membership gateway. Inventory routes and exercise each route.
- Cluster-native retention: each admitted item has one or more explicit Cluster/ChaosCluster memberships in the same atomic transaction. Classification uncertainty must be retained in an unresolved ChaosCluster.
- Producer authority: classification labels and membership weights are preserved exactly; Core only validates structural constraints and never substitutes/normalizes producer values.
- Atomicity: injected failures before/after journal append verify graph version, journal watermark, membership and idempotency state. A durable journal commit followed by projection failure must report committed/pending, never false NotCommitted.
- Replay integrity: PacketAdmission and Semantic events correlate; packet counts and recovered Coordinator count reconcile; state versions are continuous; Node/Relation/full-state digests reconcile.
- Snapshot baseline: checkpoint Context ID, state version, journal sequence/base, digest, schema, and trusted baseline. Restore baseline and replay only suffix; compare complete graph state. Validate corrupt, mismatched, missing, stale and backup snapshots.
- Compaction: compaction crash windows and backup fallback; retained references and protected floor; no silent loss; replay suffix identity.
- Reference safety: multi-membership survives unlink/archive; protected Focus/Questions/Evidence/dependencies and audit roots are never reclaimed; reclaim requires all reference obligations clear.
- Resource safety: logical budget, allocator/high-water and process RSS are separate. Rejection is atomic and visible.

Core Integrity artifacts: machine-readable report with each check ID, status (PASS/FAIL/NOT_RUN), commit SHA, profile digest, evidence path, and failure stage/code. Any NOT_RUN blocks aggregate PASS.

## 3. Recognition Quality track (external projector/model)

### Dataset and split integrity
- Versioned JSONL gold corpus with source text, expected structured semantics, evidence spans, ambiguity/abstention labels, case IDs and category tags.
- Train/prompt-development and held-out test partitions must be disjoint; corpus and prompt/profile digests pinned. CI smoke fixtures are not independent gold data.
- Include Japanese conversational and document inputs: short acknowledgement in context, topic shift, coreference, negation, contradiction, temporal revision/expiry, multiple topics, ambiguity, missing evidence, source reliability, shared cluster context, question/interest/focus, unresolved input, malformed producer output.

### Metrics
- Schema/parse validity and required-field coverage.
- Exact/normalized classification precision, recall, F1 per label and macro-F1.
- Membership-label exact match and weight preservation (Core-side invariant; exact producer value, no silent normalization).
- Evidence span precision/recall and evidence-support rate.
- Contradiction and negation accuracy; temporal update accuracy.
- Abstention precision/coverage and selective risk.
- False-positive relation rate and ambiguity preservation.
- Runtime latency p50/p95/p99, peak RSS, model file bytes, warm-up and steady-state throughput.

### Mandatory gates
- Structural validity: 100% on frozen suite; malformed outputs must be rejected or explicitly abstained, not partially accepted.
- Critical safety/context categories (negation, contradiction, temporal supersession, unsupported evidence, uncertain classification): minimum per-category recall and precision thresholds must be declared in a benchmark profile before test run. Missing thresholds means NOT_RUN.
- Overall macro-F1 and evidence-support minimums are profile-pinned before evaluation; no post-hoc threshold tuning on held-out results.
- Regression: any critical category below its floor or materially regressing versus accepted baseline blocks merge/release.
- Model selection must not optimize only JSON validity or latency.
- Results must include all cases and aggregate metrics; missing cases, model crash, timeouts or corrupt output fail closed.
- An unscored or empty independent gold corpus yields NOT_RUN, never PASS.

### Initial thresholds (provisional; must be ratified and frozen before independent corpus evaluation)
- Schema-valid outputs: 100%.
- Evidence support: >= 0.95.
- Macro-F1: >= 0.80.
- Negation / contradiction / temporal supersession / unsupported-evidence critical category recall: >= 0.90 each.
- False-positive relation rate: <= 0.05.
- Abstention: report coverage and selective risk; no fixed coverage target until corpus ambiguity is annotated.

These thresholds are proposed qualification targets, not measured performance. They must be evaluated on independently annotated held-out data and may not be silently relaxed to make a model pass.

## 4. E2E and provenance

- Same commit and pinned input/profile/corpus revisions for both tracks.
- External model outputs are untrusted proposals. Core validates structural integrity, source provenance and atomicity.
- Candidate recall, externally produced classifications and committed membership are distinct outputs with distinct IDs/statuses.
- No model may be marked approved solely because the output parses.
- Benchmark artifact retention includes raw outputs, normalized predictions, per-case errors, metrics, environment, profile and corpus digests. Sensitive source data must not be uploaded.

## 5. Conformance matrix (current known status)

| Requirement | Status before this work | Evidence / follow-up |
|---|---|---|
| Typed Core and journal-backed Graph | Partial/implemented | Existing C++ tests and CI |
| Cluster-native mandatory ingress | Incomplete per v1.0.1 implementation-status section | Inventory all ingress and enforce gateway |
| Unresolved ChaosCluster retention | Incomplete | Admission conformance across public APIs |
| Producer labels/weights preserved | Contract exists; route coverage incomplete | Cross-route tests |
| Snapshot baseline restore after compaction | Gap | Trusted snapshot restore and suffix replay |
| Full Graph digest audit | PR #156 draft only | Rebase/current-base review and merge gate |
| External model structured output | Parse smoke exists | Parsing alone is not quality |
| Independent semantic gold corpus | Absent/NOT_RUN | Create double-annotated corpus and adjudication |
| Critical semantic quality gates | 0/6 markers in latest model smoke | Implement metrics and fail-closed thresholds |
| Target-device RSS/latency qualification | Not measured | Declared device profile and sustained run |

## 6. Work sequencing (parallel)

### Track A
1. Rebase/reconcile Journal Replay Integrity PR against current main.
2. Snapshot baseline contract and snapshot schema audit.
3. Implement trusted snapshot restore, state version/journal sequence reconciliation, suffix replay.
4. Corruption, wrong-context, wrong-version, wrong-digest, backup selection and fault injection.
5. Cross-platform and sanitizer gate.

### Track B
1. Inventory current benchmark runner, prompts, fixture and output schema.
2. Replace marker-only checks with case-level semantic scoring and strict gates.
3. Add frozen test corpus format and corpus/profile digest.
4. Add failure cases for invalid JSON, unsupported claims, missing evidence, wrong spans, negation, contradiction, temporal revisions and ambiguous context.
5. Run model matrix with pinned settings; publish per-case/aggregate report and raw output artifacts.
6. Keep quality gate separate from runner/infra success.

### Integration
- Add final aggregate gate that depends on both track workflows and validates commit/profile/corpus digest equality.
- No release/merge readiness while either track is FAIL or NOT_RUN.
- Hardware resource qualification remains a separately labeled target-device requirement; CI runner numbers are regression indicators only.

## 7. Current known model smoke result (reference only)

Latest checked CPU projector CI log:
- Qwen3 0.6B Q4_K_M: 5/5 parseable JSON, 0/6 expected semantic markers, mean 5.961 s, peak RSS about 5.12 GiB.
- SmolLM2 135M Q4_K_M: 5/5 parseable JSON, 0/6 expected semantic markers, mean 1.8484 s, peak RSS about 346 MiB.
- Quality gate FAIL for both. This small smoke set is not Japanese semantic accuracy measurement.

This document is a conformance work plan. It does not indicate that these gates have been implemented, run, or merged.
