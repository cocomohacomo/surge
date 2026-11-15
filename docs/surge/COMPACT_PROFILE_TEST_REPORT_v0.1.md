# SURGE v0.8 Compact Profile Thought-Experiment Report v0.1

Date: 2026-11-16
Target: 30MB-class quantized non-autoregressive classifier
Status: Design-level review; no executable model benchmark performed

## 1. Evaluation boundary

This report tests whether the v0.8 interface can preserve v0.7 semantic contracts while using bounded local classification. It does not measure a real model, CPU runtime, accuracy, RSS, or RTF. PASS means the interface specifies a safe handling path; it is not an empirical result.

The preserved v0.7 baseline includes W01-W50 and adversarial checks A-H. Those fixtures remain mandatory and have not been rerun against an executable v0.8 implementation.

## 2. Compact profile cases

| ID | Test input | Required result | Design result |
|---|---|---|---|
| C01 | One-sentence input | classify locally; importance is not inferred from length | PASS |
| C02 | One-word acknowledgement | retain context dependence; abstain if ambiguous | PASS |
| C03 | Paragraph split across packets | continuation and offsets converge; no duplicate Evidence | PASS |
| C04 | Same content with different packet boundaries | equivalent final semantic projection | PASS |
| C05 | Heading followed by short paragraph | inherit heading scope through structural refs | PASS |
| C06 | Table cell with header refs | preserve header, unit, denominator and cell anchor | PASS |
| C07 | Table cell without header refs | OPAQUE/UNMAPPED; no scoped numerical Claim | PASS |
| C08 | Quote plus commentary | preserve source/speaker attribution separately | PASS |
| C09 | Mixed code and prose | preserve CODE boundary and language | PASS |
| C10 | Japanese homograph | preserve alternatives or abstain; no forced merge | PASS |
| C11 | Negation and modality | preserve polarity/modality cues | PASS |
| C12 | Event date vs publication/retrieval date | preserve distinct temporal roles | PASS |
| C13 | Same URL updated | append immutable snapshot; bounded Impact Set | PASS |
| C14 | Syndicated copies | no independent-source inflation | PASS |
| C15 | Prompt injection in source text | untrusted data; no policy override | PASS |
| C16 | Unsupported language/domain | explicit abstention; Evidence retained | PASS |
| C17 | Competing low-margin labels | multi-candidate or abstain | PASS |
| C18 | Stale ContextHint | Core revalidation before commit | PASS |
| C19 | Overlapping packet windows | idempotent Evidence and Unit integration | PASS |
| C20 | Burst input / queue pressure | backpressure or defer; no silent loss | PASS |

Design-level result: 20/20 handling paths specified.

## 3. v0.7 semantic preservation review

| Capability | v0.8 preservation mechanism | Result |
|---|---|---|
| PageSnapshot immutability | packet references immutable snapshot and digest | PASS |
| ContentBlock hierarchy | Adapter supplies structural-first metadata | PASS |
| SourceAnchor integrity | exact source offsets and offset_basis | PASS |
| Coverage/partial capture | explicit coverage and omitted ranges | PASS |
| Table semantics | row/column/header/unit/footnote refs | PASS |
| Quote attribution | separate attribution refs | PASS |
| Source lineage/independence | Core-owned graph, never model-owned | PASS |
| Temporal revision | separate time roles and append-only snapshots | PASS |
| Question Aspect lifecycle | Core-owned state machine | PASS |
| Interest/Focus separation | Core-owned policy and transitions | PASS |
| Dependency/SCC convergence | bounded Core reconciliation | PASS |
| Transaction/recovery | unchanged Core commit and event-log rules | PASS |
| Prompt-injection resistance | model output and source text remain untrusted | PASS |

These are contract-level checks, not execution results. No semantic equivalence score has been measured.

## 4. Performance status

| Requirement | Status |
|---|---|
| Model artifact <= 30 MiB | TARGET, unmeasured |
| Media-stream RTF < 0.01 | TARGET, unmeasured |
| Japanese text reference RTF < 0.01 | TARGET, unmeasured |
| p50/p95/p99 packet latency | NOT MEASURED |
| sustained queue stability | NOT MEASURED |
| peak/steady RSS | NOT MEASURED |
| semantic projection equivalence | NOT MEASURED |

For text-only tests, the v0.8 benchmark convention is T_ref = normalized Japanese characters / 20 chars per second. Text RTF is wall-clock processing time divided by T_ref. It must be reported together with chars/s and packet latency. This convention is not a human reading-speed claim.

## 5. Risks and remaining proof obligations

1. A 30MB artifact does not guarantee adequate Japanese semantic classification accuracy.
2. Hashed n-gram models may confuse long-distance scope, negation, ellipsis, homographs and implicit references.
3. The compact model must be trained/evaluated on representative Japanese web, document, conversation, table, code and quote examples.
4. Core-side structural rules must not be mistaken for a replacement for semantic evidence.
5. A fast classifier that abstains excessively may meet latency but fail usefulness; report coverage and selective risk.
6. End-to-end RTF must include packetization, ContextHint retrieval, validation and Core work, not just model inference.
7. No claim that RTF <0.01 or full semantic parity has been achieved is permitted until runtime tests are run.

## 6. Conclusion

The v0.8 interface design preserves the full v0.7 semantic contract by moving only local recognition into a bounded compact classifier and retaining cross-event cognition in the Core. All 20 compact-profile thought experiments have a defined safe path. Runtime performance, model accuracy and semantic parity remain open empirical acceptance gates.
