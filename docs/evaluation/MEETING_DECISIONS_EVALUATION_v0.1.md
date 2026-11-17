# Meeting Decision Evaluation Protocol v0.1

## 1. Single use case

**Japanese meeting minutes: extract explicit decisions and assigned action items with owner, deadline and exact source evidence.**

In scope: a decision actually reached; an action assigned to a person or role; an explicit deadline/date; and the exact source span supporting the item.

Out of scope for this first benchmark: generic summarization, sentiment, speaker diarization, legal interpretation, inferred owners/deadlines and broad document QA. If a field is not explicit, it must remain null or the system must REVIEW/ABSTAIN.

## 2. Independent gold-data protocol

The repository does not contain an independent gold corpus yet. Do not use model-generated labels, developer-authored scoring examples or the 50-case scoring regression set as gold.

1. Sample complete meetings from sources for which reuse and privacy permissions are documented. Split by meeting, never by utterance, to prevent leakage.
2. Two annotators independently label every decision/action, owner, deadline, criticality and exact UTF-8 byte span in NFC-normalized source text.
3. A third adjudicator resolves every disagreement. Preserve both original annotations and the adjudication record.
4. Freeze source, gold, split manifest and taxonomy; compute SHA-256 digests before running any system.
5. Keep a held-out test split inaccessible to prompt/model development. Report inter-annotator agreement before adjudication.
6. Include hard negatives: proposals not approved, conditional statements, rejected actions, tentative plans, corrections, conflicting dates, unnamed owners and unresolved references.
7. Store licensing/consent, redaction, sampling method, annotator IDs (pseudonymous), annotation guide version and adjudication provenance in the dataset manifest.

Recommended pilot size: at least 100 meetings and 500 adjudicated items, with at least 50 critical items per critical label. If a label denominator is below the registered minimum, its result is INCONCLUSIVE, not PASS. Increase the sample before making a general effectiveness claim.

## 3. Common adapter contract

All systems receive identical JSONL case records: `case_id`, `source_text`, `source_sha256`, `language` and immutable `metadata`.

Every adapter emits exactly one record per case:
- `case_id`, `source_sha256`
- `decision`: ACCEPT, REVIEW or ABSTAIN
- `items[]`: `action`, `owner`, `deadline`, `evidence_quote`, `evidence_start`, `evidence_end`, `source_sha256`

Offsets are half-open UTF-8 byte offsets in NFC text. A prediction is grounded only when the exact quoted bytes match the source and digest. No adapter may fill a missing owner or deadline by inference.

Use `tools/run_meeting_decision_adapter.py` to connect a module exposing `predict(case)`. Use `tools/evaluate_meeting_decisions.py` to score the resulting JSONL.

## 4. Baselines and equal conditions

Compare at least four systems:

| System | Retrieval / state | Purpose |
|---|---|---|
| Simple RAG | fixed chunking + BM25 or vector top-k + one extraction prompt | lowest-complexity retrieval baseline |
| Structured DB | deterministic schema/table extraction + SQL/query rules | structured-state baseline |
| Existing platform | one named production minutes/action-item product or existing internal workflow | practical incumbent |
| SURGE | same source, same base model, typed Evidence/Claim/relations, temporal and dependency handling | proposed framework |

Control the base model/version, temperature, prompt budget, maximum retrieved tokens, source normalization, hardware, concurrency, retry policy and output schema. Where a system inherently requires a different model, report that as a separate track rather than pretending it is controlled.

- **Track A:** same LLM and extraction prompt; vary only context/state architecture.
- **Track B:** each system's best documented configuration, with model and cost differences disclosed.
- Run at least 3 times for nondeterministic systems; report mean and dispersion.
- Use identical case order and held-out test set; do not manually correct outputs after generation.

Report setup/operation cost and complexity separately from quality.

## 5. Metrics and gates

Primary:
- micro precision, recall and F1 for canonical action/decision items;
- critical-label recall and critical false-negative rate;
- owner accuracy and deadline accuracy on matched items;
- exact evidence grounding rate;
- case coverage (ACCEPT cases / all cases);
- selective risk (non-exact accepted cases / accepted cases);
- exact-case accuracy.

Operational:
- end-to-end latency p50/p95/p99, throughput, peak RSS, CPU utilization, model artifact size, tokens/cost per meeting, journal growth and recovery time.

The evaluator computes a deterministic pilot gate from pre-registered thresholds. Defaults (coverage >= 0.70, critical recall >= 0.90, selective risk <= 0.10) are pilot starting values, not achieved results. Freeze thresholds and profile digest before opening test results. Zero denominator or missing independent gold yields INCONCLUSIVE.

## 6. Demonstrating SURGE's incremental value

The framework's added complexity is justified only if it provides reproducible benefit that simpler baselines do not:
- fewer critical false negatives when decisions are conditional, corrected or contradicted later;
- higher evidence grounding and temporal consistency;
- lower duplicate action items and fewer stale owner/deadline values after corrections;
- acceptable coverage and latency at a documented resource budget.

If SURGE does not materially outperform simpler systems on the primary metric or operational constraint, retain the simpler system for this use case. Architecture novelty is not a substitute for measured benefit.

## 7. Current status

This protocol and evaluator are scaffolding. There is no independent gold corpus in the repository and no comparative effectiveness result yet. The first evaluation result must remain INCONCLUSIVE until the gold corpus is independently annotated, adjudicated, frozen and evaluated on a held-out split.
