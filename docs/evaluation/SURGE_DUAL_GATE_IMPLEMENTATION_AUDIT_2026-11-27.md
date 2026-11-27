# SURGE Dual-Gate Implementation Audit (2026-11-27)

Target: docs/evaluation/SURGE_SPEC_CONFORMANCE_DUAL_GATES_v0.1.md and docs/surge/SPECIFICATION_v1.0.md.
Scope: evaluator, model-matrix runner/summarizer, aggregate evidence path.
This audit records code-level conformance only. It is not a Core Integrity or Recognition Quality qualification.

## Findings fixed in this increment

14. Migrated the eight-case synthetic seed corpus to `surge-gold-corpus-v1.0`; every Gold attribute and relation now has exact source-specific Evidence Span annotations.
15. Added independently scored Negation, Contradiction, Temporal Supersession and Ambiguity case labels with per-case and aggregate Precision/Recall/F1.
16. Benchmark profiles now require both category Precision and Recall floors, with exact Gold-category coverage and bounded finite thresholds.
17. Added an immutable model lock for llama.cpp, model and embedded-tokenizer revisions; CI checks out the exact llama.cpp SHA and the runner downloads each GGUF at its pinned revision.
18. Experiment manifests record source/runtime identity, model and tokenizer revisions, downloaded file hashes, prompt hash, generation settings, environment and corpus/profile hashes.

9. Added canonical Projector Output JSON Schema v1.0 and independent Draft 2020-12 validator.
10. Model Matrix prompt now requests schema-versioned canonical output and validates each parsed response before scoring.
11. Added exact Evidence Span scoring using source-specific Unicode code-point offsets and exclusive end positions; invalid offsets/quotes are malformed.
12. Attribute and relation Evidence Spans are measured independently and jointly with Precision/Recall/F1.
13. Model Matrix execution fails when canonical schema validation fails.

1. Empty gold/prediction sets no longer produce precision/recall 1.0. Undefined denominators score zero.
2. Prediction case IDs must exactly equal held-out test case IDs. Missing and extra IDs fail closed.
3. Duplicate corpus case IDs and train/development-to-test overlap are rejected.
4. Missing predicted category is explicitly scored as __missing_category__, while the expected category receives a false negative.
5. Model-matrix execution requires an explicit expected model count and exact expected case-ID coverage.
6. Duplicate/missing model names, duplicate/missing/extra cases, invalid JSON, timeout, nonzero exit, missing RSS and incomplete semantic metrics fail execution.
7. The runner now emits the expected case-ID set and corpus/profile digests.
8. The strict prediction schema no longer accepts the legacy candidates alias in place of canonical attributes.

## Follow-up refactor: constrained projector execution (2026-11-27)

- The first constrained run on commit c36b12d failed before model loading. The runner invoked llama-cli with --no-conversation, but the pinned llama.cpp revision exposes that option only for the LLAMA_EXAMPLE_COMPLETION program. The previous CLI contract test only inspected the argument list and did not verify it against the pinned executable. This was a confirmed integration-test gap.
- The pinned source shows that llama-cli is a conversation UI: it writes a banner, echoes the prompt, and prints an exit message. Its stdout is therefore incompatible with the runner's strict stdout-is-one-JSON-object contract even if its arguments are corrected.
- The runner and CI now build and invoke the pinned llama-completion target. This is the non-interactive completion executable; --no-conversation, --no-display-prompt, --simple-io, --json-schema-file and --log-file are supported in the pinned revision. The CLI-only --no-show-timings flag was removed.
- The preformatted ChatML prompt is sent as a completion prompt with conversation wrapping disabled. Model output remains isolated on stdout; runtime diagnostics are retained separately.
- Timeout handling now starts a new process session and terminates/reaps the process group, preventing the /usr/bin/time wrapper from leaving the model process alive. Diagnostic logs are read before the temporary log directory is removed.
- Model Lock validation rejects duplicate names, unpinned revisions, malformed Hugging Face repository IDs, non-boolean enablement flags, disabled entries without reasons and unsafe GGUF paths before model download.
- Gold corpus preflight verifies unique case IDs, canonical schema conformance and exact Unicode code-point offsets for every annotated evidence span.
- The matrix report is atomically persisted before execution and after each case so failed model acquisition or generation retains actionable evidence. The experiment manifest identifies llama-completion and its exact runtime version.
- Regression tests cover the executable contract, version-specific arguments, invalid lock entries, corpus identity/evidence offsets, nonzero-exit JSON rejection and timeout process cleanup.
- These changes repair the execution harness only. They do not claim improved model quality. The synthetic seed corpus remains non-independent, so Recognition Quality must remain NOT_RUN.

- The pinned completion executable appends the literal ` [end of text]` stdout footer after EOS. The strict JSON parser now strips only this exact terminal marker and still rejects banners, prompt echoes, code fences and all other trailing or leading text. A dedicated regression test covers both acceptance of this known footer and rejection of preceding noise.
- The Summarizer now counts any explicit per-case `execution_error` as a matrix execution failure, including a failure diagnostic returned alongside valid JSON in the final expected case. This closes a last-case-only fail-open edge case.

## Remaining conformance gaps

### Recognition Quality

- The checked-in corpus is synthetic seed data with independent_adjudication=false; Recognition Quality MUST remain NOT_RUN.
- The migrated seed corpus has exact spans, but they are synthetic engineering annotations, not independently adjudicated. Recognition Quality MUST remain NOT_RUN.
- Four phenomenon dimensions now have independent binary Precision/Recall/F1. They remain engineering-only until a genuinely independent held-out annotation set is adjudicated.
- Both category Precision and Recall floors are profile-pinned with exact Gold-category coverage. A production profile still requires domain-owner review before benchmark qualification.
- Required aggregate, schema, evidence, category and phenomenon thresholds are now validated as finite bounded values; missing or unexpected category/phenomenon floors fail closed.
- Model/runtime sources are now pinned through `config/model-matrix-lock-v1.0.json`; Hugging Face revisions are short commit identifiers and should be upgraded to full 40-character SHAs if the Hub API metadata is available in the execution environment. The report records downloaded file hashes.
- The experiment manifest records model/runtime revisions, runtime versions, prompt/template hashes, generation settings and corpus/profile identities. Compiler/CMake flags and CPU feature flags should be added to the manifest in a follow-up.
- Candidate recall quality from the cognition specification (recall@k, diversity, ranking quality, ambiguity preservation, cross-cluster recovery and pinned-state determinism) is outside the current projector matrix.

### Core Integrity

- The workflow runs CTest and Python smoke tests but deliberately emits core_integrity_gate=NOT_RUN.
- Full specification audit remains outstanding: all ingress routes, atomic Admission/ChaosCluster membership, producer-label/weight preservation, Packet-to-Semantic Journal correlation, full Graph digest, trusted Snapshot baseline restore, suffix replay, compaction crash windows, backup fallback, reference closure and allocator/RSS separation.
- PR #156 replay-integrity work and Snapshot baseline integration require reconciliation against current main and a single end-to-end fault-injection suite.

### Aggregate gate

- The aggregate script requires both track statuses to be exactly PASS and checks commit/profile/corpus identity equality.
- Aggregate PASS remains prohibited until both independent evidence tracks are complete and use the same immutable experiment identity.

## Next recommended implementation sequence

1. Canonical Projector JSON Schema v1.0 and independent validation are implemented. Remaining work: migrate all external adapters/output fixtures and require schema conformance for every ingestion route.
2. Extend independent annotation with exact evidence spans, category precision/recall floors, explicit negation/contradiction/temporal labels and ambiguity/abstention labels.
3. Review and freeze production benchmark thresholds against the independently annotated task distribution before collecting held-out results.
4. Expand Hugging Face short commit prefixes to full commit SHAs when available; add compiler/CMake/CPU feature provenance to the manifest.
5. Add case-level candidate recall/ranking benchmarks from the cognition specification, keeping candidate proposals distinct from committed memberships.
6. In Core, complete Snapshot baseline trust/restore plus Journal suffix replay and full Graph digest reconciliation, then add crash injection across commit, projection, compaction and backup fallback.
7. Only then run the independent held-out corpus and full Core integrity suite; aggregate qualification remains fail-closed until both pass.

## Current qualification status

- Core Integrity: NOT_RUN
- Recognition Quality: NOT_RUN
- Aggregate: FAIL by design until both evidence tracks are independently complete.