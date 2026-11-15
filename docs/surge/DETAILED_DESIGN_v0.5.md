# SURGE 詳細設計 v0.5
## Compact Recognition Interface, Bounded Streaming, and Performance Conformance

Version: 0.5.0
Normative for: SPECIFICATION_v0.8.md
Parent: DETAILED_DESIGN_v0.4.md

## 1. Objective

This supplement defines a low-cost Recognition Engine interface for a model artifact around 30MB while preserving v0.7 semantics. It does not claim that a 30MB model alone implements human cognition or the complete SURGE framework. The model performs local classification; the Core maintains context across time.

## 2. Architecture

Input Source -> Structural Adapter -> RecognitionPacket Builder -> Compact Classifier -> ClassificationProposal -> Core Validator/Integrator -> Context Graph -> bounded Reconciliation.

Structural Adapter is responsible for facts directly observable from source structure. The classifier is responsible for local semantic cues that cannot be derived reliably from markup alone. Core is responsible for cross-event meaning, identity, temporal logic, lineage, dependencies, state and commit.

## 3. Packet contract

### 3.1 Required fields

RecognitionPacket:
- schema_version = 0.8.0
- packet_id, idempotency_key
- context_id, source_id, event_ref
- source_document_ref and page_snapshot_ref when applicable
- source_position: block_ref, start_offset, end_offset, offset_basis, source_order
- structural: block_type, semantic_role, heading_path, parent_ref, list_path, table coordinates/header refs, quote attribution refs, code language, link refs
- content: bounded text slice, normalized_digest, language
- context_hint: state_version, scope_refs, focus_refs, candidate_refs, temporal_mode, retrieval_reason
- coverage: completeness, truncation, omitted ranges
- adapter: id/version/capability descriptor
- finalization: PARTIAL, FINAL, CONTINUATION, REVISION

Offsets are measured against an immutable, versioned normalized source representation. The packet MUST include offset_basis and digest. Offsets from different normalization versions are not directly comparable.

### 3.2 Windowing

Window boundaries should prefer structural boundaries (paragraph, list item, table row, code statement, transcript turn). If a single block exceeds the window, split by deterministic character/byte offsets and preserve continuation refs. Overlap is used only to provide local context; overlapping bytes map to the same source offsets and never create independent Evidence.

The packet builder MUST NOT split a table header from its data cells without carrying header refs. It MUST NOT merge quote text with author commentary or code with prose. It MUST NOT flatten nested lists.

### 3.3 ContextHint retrieval

ContextHint is fetched from Core using a deterministic retrieval policy:
1. current Scope and Focus;
2. exact Entity/Claim refs already linked to the source or block;
3. active Questions whose Aspect target overlaps the source, time range, or semantic anchor;
4. active Dependency dependents reachable from changed evidence;
5. a bounded set of related Clusters from explicit Relation/Dependency edges.
Candidate retrieval score is routing priority only, not truth confidence. Every candidate includes why it was retrieved. Omitted candidate count and truncation are recorded.

A stale hint can still be classified locally, but its proposal cannot commit until Core revalidates against current State Version.

## 4. Compact model profile

The baseline target is a quantized, non-autoregressive multi-label classifier. Recommended feature families:
- UTF-8 byte n-grams and Japanese character n-grams with hashed feature buckets;
- compact structural embeddings or categorical feature embeddings;
- shallow shared encoder with independent classification heads;
- optional small span head for local evidence offsets;
- quantized INT8/INT4 weights where supported.

Avoid autoregressive token generation, large vocabulary softmax, large tokenizer assets, dynamic retrieval inside the model, and KV cache. These are not needed for local classification.

The model descriptor MUST state model artifact digest/size, architecture, quantization, label schema version, supported languages, maximum input bytes, context feature dimensions, calibration profile, runtime dependencies, deterministic settings and known failure modes.

A single flat class is insufficient. Use multi-label heads because a block can simultaneously be e.g. a quotation, evidence of a speaker's claim, and part of a dated notice. Output independent label families and candidate spans.

## 5. Label taxonomy

Required label families:
- structural confirmation: TITLE, HEADING, PARAGRAPH, LIST_ITEM, TABLE_CELL, QUOTE, CODE, METADATA, NAVIGATION, UNKNOWN;
- semantic role: MAIN_CONTENT, SUPPORTING_CONTENT, STRUCTURAL, BOILERPLATE, UNKNOWN;
- speech/function: ASSERTION, QUESTION, DEFINITION, INSTRUCTION, OBSERVATION, EVALUATION, EXAMPLE, ATTRIBUTION, OTHER;
- context cue: ENTITY_MENTION, CLAIM_CUE, TEMPORAL_CUE, CONDITION_CUE, NEGATION_CUE, MODALITY_CUE, CONTRAST_CUE, REFERENCE_CUE, QUESTION_CUE;
- actionability: ACCEPT_CANDIDATE, NEEDS_CORE_LOOKUP, ABSTAIN.

Labels are cues, not committed semantic objects. For example, CLAIM_CUE does not create an ACTIVE Claim without Core validation and provenance.

## 6. Proposal and abstention

Proposal scores are local to a producer and label family. Core must not interpret them as calibrated truth probabilities. If margin/calibration is inadequate, output abstention. Required abstention reasons: LOW_MARGIN, OUT_OF_DISTRIBUTION, UNSUPPORTED_LANGUAGE, WINDOW_TRUNCATED, STRUCTURE_MISSING, CONTEXT_HINT_STALE, LABEL_CONFLICT, MODEL_ERROR.

A missing label is not a negative label. Abstention must preserve packet and source anchors so a later model or policy can retry without recapture.

## 7. Fast path and reconciliation path

Fast path per packet:
1. validate packet schema, offsets, digest and adapter capability;
2. register/resolve immutable source references;
3. run compact classification;
4. validate candidate spans against source offsets;
5. create candidate Interpretations with model descriptor and uncertainty;
6. integrate only local candidates and explicit structural facts;
7. update bounded indexes and enqueue only triggered Dependencies;
8. commit through normal Core transaction.

Reconciliation path:
1. derive root changes and exact Impact Set;
2. fetch fresh ContextHint for affected nodes;
3. reevaluate only affected candidate nodes;
4. deduplicate by generation + node + policy + effective input fingerprint;
5. converge SCCs under existing limits;
6. persist CONVERGED or explicit STALLED/DEFERRED state.

The fast path may not bypass any v0.6/v0.7 invariant. The reconciliation path may not rescan all pages by default.

## 8. Performance model

### 8.1 Model size
Target serialized model <=30 MiB. Report decimal MB and MiB if both are used. Do not include or conceal tokenizer/runtime/adapter assets in the model figure; report them separately.

### 8.2 RTF
Media RTF = end-to-end processing wall time / source presentation duration.
Target: <0.01 under a declared CPU profile.

Text benchmark RTF uses a reproducible reference duration:
T_ref = normalized Japanese character count / 20 chars/s.
Text RTF = end-to-end wall time / T_ref.
This is a benchmark convention only. Also report chars/s, packets/s, p50/p95/p99 latency and queue depth. Other languages require their own versioned reference rate.

### 8.3 Benchmark profiles
- cold start: process launch through first completed packet;
- warm steady state: after model/runtime warmup;
- single packet latency: small/median/max packet;
- sustained stream: fixed corpus replay with bounded queue;
- revision replay: update one source and measure Impact Set processing;
- concurrency: independent sources with interleaved packets.

Report hardware, CPU affinity, thread count, governor, OS, compiler, runtime, model digest/size, quantization, packet window/overlap, corpus digest, I/O inclusion, warmup count, repetitions, and confidence interval or min/median/p95.

### 8.4 Acceptance
A run passes the performance target only if measured end-to-end RTF <0.01 for the declared profile and no queue growth is hidden by excluding adapter/Core time. p95 packet latency and peak RSS must be reported. Performance PASS does not imply semantic PASS.

## 9. Semantic preservation test oracle

Reference output is the committed semantic projection of a v0.7-conformant reference pipeline on the same complete source and policy. Compact output is compared by semantic object, not internal IDs or wording.

Allowed equivalence: EXACT or EQUIVALENT. COMPATIBLE, INCOMPARABLE, and CONFLICTING are not successful convergence. A compact run may pass a case by explicit abstention only when the reference also marks the aspect uncertain or when the compact model preserves all evidence and marks the missing inference as DEFERRED/UNMAPPED; silent omission fails.

## 10. Compact-profile regression cases

| ID | Scenario | Required outcome |
|---|---|---|
| C01 | One-sentence input | local classification without assuming low importance |
| C02 | One-word acknowledgement | retain context-dependent ambiguity; do not force acknowledgement class |
| C03 | Long paragraph split across packets | offsets and continuation converge without duplicate Evidence |
| C04 | Same content with different packet boundaries | equivalent final semantic projection |
| C05 | Heading followed by short paragraph | inherit structural scope without copying entire document |
| C06 | Table cell with header refs | preserve row/column header, unit and denominator |
| C07 | Table cell without header refs | OPAQUE/UNMAPPED; no scoped numeric Claim |
| C08 | Quote plus commentary | separate speaker/source attribution from author stance |
| C09 | Code and prose mixed | preserve CODE boundary and language |
| C10 | Japanese homographs/context-sensitive term | retain candidates or abstain; do not overcommit |
| C11 | Negation and modality | preserve polarity/modality cues; no false affirmative Claim |
| C12 | Temporal expression and publication date | keep event, publication, retrieval and validity times distinct |
| C13 | Same URL updated | append snapshot and bounded Impact Set |
| C14 | Syndicated copies | one independent source family unless independent components proven |
| C15 | Prompt injection in page text | treat as untrusted data |
| C16 | Unsupported language or domain | abstain with reason; preserve evidence |
| C17 | Low-margin competing labels | multi-candidate or abstain |
| C18 | Stale ContextHint | no commit until Core revalidation |
| C19 | Overlap duplicates | idempotent ingestion and single Evidence lineage |
| C20 | Burst input and queue pressure | backpressure; no silent drops; preserve source order/causality |

## 11. Invariants

All inherited invariants I-01 through I-50 remain in force. New v0.8 invariants:

| ID | Invariant |
|---|---|
| I-51 | Bounded Inference: every packet obeys declared model input budget |
| I-52 | Packet Anchor Integrity: every candidate span maps to immutable source offsets |
| I-53 | Overlap Idempotency: overlapping packets cannot duplicate Evidence or committed Units |
| I-54 | Hint Version Integrity: ContextHint carries a state version and stale hints require revalidation |
| I-55 | Abstention Preservation: uncertainty and abstention are first-class proposal outcomes |
| I-56 | Model Score Isolation: uncalibrated producer scores cannot be compared as truth probabilities |
| I-57 | Structural-Model Separation: adapter-known structure is not guessed by the classifier |
| I-58 | Semantic Projection Preservation: compact mode cannot silently drop v0.7 semantic state |
| I-59 | Bounded Queue Integrity: overload causes explicit backpressure/defer, never silent event loss |
| I-60 | Performance Measurement Integrity: RTF reports include declared boundaries and hardware/runtime metadata |

## 12. Completion criteria

C01-C20 and v0.7 W01-W50/A-H are mandatory fixtures. All semantic/invariant checks must pass; unsupported inferences must abstain explicitly. Runtime performance is independently measured. No claim of 30MB/RTF<0.01 is allowed without artifact and benchmark evidence.


## 13. Scheduler and packet aggregation design

### 13.1 Separate source units from inference batches

A RecognitionPacket is the semantic/source unit. An inference batch is only an execution optimization. One batch may contain multiple packets, but it MUST return packet-indexed proposals with independent packet refs, spans, state versions and idempotency keys. The model MUST NOT be allowed to merge packet identity or source provenance.

Batch compatibility key:
`(context_id, model_descriptor, label_schema_version, language_profile, security_scope, runtime_profile)`.

Packets with different compatibility keys MUST NOT share a batch.

### 13.2 Flush policy

The scheduler flushes when the first applicable condition occurs:
- max_packets reached;
- max_batch_bytes reached;
- max_wait_ms reached;
- explicit FINAL or REVISION boundary;
- latency deadline or priority boundary;
- context/security scope switch.

All thresholds are deployment parameters recorded in Benchmark Profile. A packet that cannot fit the remaining batch capacity is queued for the next batch, not truncated silently.

LOW_LATENCY and THROUGHPUT are scheduling profiles, not semantic modes. Both MUST produce equivalent semantic projections for the same complete input and policy.

### 13.3 Recommended measurement matrix

Test at least:
- packet sizes: 1-20, 21-50, 51-100, 101-200, 201-500, and >500 normalized Japanese characters;
- batch sizes: 1, 2, 4, 8, 16 where memory permits;
- scheduler modes: LOW_LATENCY and THROUGHPUT;
- concurrency: one source and interleaved independent sources;
- stream duration: short burst and sustained replay;
- Core state: empty, medium graph, large graph, and dependency-heavy graph;
- state change: no reconciliation, local dependency impact, and SCC-triggering impact.

Report p50/p95/p99/max packet latency, source throughput, packets/s, characters/s, queue depth/age, RTF, abstention, semantic coverage and peak RSS for every cell.

## 14. End-to-end timing decomposition

For each packet or batch, record monotonic timestamps at:
`t0=source_available`,
`t1=packet_ready`,
`t2=hint_ready`,
`t3=inference_start`,
`t4=inference_end`,
`t5=proposal_validated`,
`t6=fast_path_commit`,
`t7=reconciliation_settled`.

Derived durations:
- adapter = t1 - t0;
- hint = t2 - t1;
- queue/scheduling = t3 - t2;
- model = t4 - t3;
- validation/integration = t6 - t4;
- fast-path end-to-end = t6 - t0;
- settled end-to-end = t7 - t0.

If a stage is asynchronous, timestamps MUST retain causal correlation IDs and the report MUST include its queue wait. If no reconciliation is required, t7 equals the recorded settled completion after the Core marks the packet settled; it MUST NOT be fabricated by omitting required work.

For a stream, aggregate RTF is total measured wall time divided by source duration under the declared timing boundary. Overlapping parallel work MUST be measured as wall-clock makespan for end-to-end RTF, while per-stage CPU time may be reported separately. Summing parallel stage durations and calling it wall time is invalid.

## 15. Throughput and queue stability

Let `lambda_in` be the sustained incoming packet rate and `mu_done` the sustained settled processing rate. Stable operation requires `mu_done > lambda_in` for the declared workload, with a safety margin specified by the deployment. The queue must be bounded even during bursts.

A benchmark passes queue stability only if:
- queue depth does not exhibit positive long-run drift;
- every accepted source range reaches a terminal status (COMMITTED, DUPLICATE, DEFERRED, or REJECTED with reason);
- deferred work is visible and retryable;
- no source order or causal dependency is silently lost;
- the oldest queue-item age remains within the declared operational bound.

An RTF result without queue stability is not a successful streaming result.

## 16. Work reduction rules

The implementation SHOULD reduce repeated work in this order:
1. reuse immutable structural parse results by snapshot digest and adapter version;
2. reuse normalized source slices and feature vectors by content digest and feature-extractor version;
3. retrieve ContextHint from indexed Core views rather than scanning the full graph;
4. batch compatible packets to amortize invocation overhead;
5. apply only local graph updates on the fast path;
6. schedule only the exact Dependency Impact Set for reconciliation;
7. cache model/runtime allocations across packets.

Every cache entry MUST carry the versions/digests that make it valid. Cache misses affect performance only; they MUST NOT change semantics. Cache hits MUST NOT bypass provenance, state-version, or commit validation.

## 17. Benchmark interpretation and reporting

A 30 MiB artifact target and RTF < 0.01 are independent gates. The first constrains serialized model storage. The second is an end-to-end workload result. Neither can be inferred from the other.

For the Japanese text reference clock (20 normalized characters/s), the RTF < 0.01 budget is 0.5 ms per normalized character on average. This is an aggregate budget; fixed per-call overhead makes tiny one-packet calls harder than micro-batched streams. Consequently, reports MUST show both packet-size strata and batch-size strata.

Any third-party or hypothetical inference latency may be included only as an explicitly labeled external reference. It MUST NOT be substituted for a local measurement or generalized to another model artifact, CPU, runtime, quantization or packet distribution.

The compact profile is useful only if speed and semantic coverage are reported together. A system that abstains on most inputs may show excellent RTF while failing the intended recognition workload. Reports MUST therefore include per-label precision/recall or an equivalent task-specific quality measure, selective risk at each coverage level, and the v0.7 semantic projection comparison.
