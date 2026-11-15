# SURGE 基礎設計 v0.8
## Universal Context Cognition Framework
### Resource-Bounded Recognition Interface / 30MB-Class Model Profile

Version: 0.8.0
Status: Architecture and Core Contract Specification
Parent: SPECIFICATION_v0.7.md
Detailed design: DETAILED_DESIGN_v0.5.md
Input contract: schemas/surge-v0.8-input.schema.json
Proposal contract: schemas/surge-v0.8-proposal.schema.json
Core API extension: docs/surge/CORE_API_v0.3.md

## 1. Version policy

v0.8 inherits v0.7 and v0.6 in all non-conflicting clauses. It does not remove, weaken, or collapse any v0.7 semantic object, invariant, lifecycle, provenance rule, temporal distinction, or reconciliation behavior. It changes the Recognition Engine input/output boundary and adds a resource-bounded execution profile.

The 30MB-class profile is a model deployment target, not a claim that the entire SURGE runtime, adapters, dictionaries, repository, or working memory must fit in 30MB. Model artifact size, resident memory, adapter memory, Core memory, and repository storage MUST be reported separately.

## 2. Design principle: preserve cognition, bound each inference

A small Recognition Engine MUST NOT be asked to reconstruct the entire Context graph from raw text. It receives a bounded RecognitionPacket containing a local content window, typed structural metadata, and a compact ContextHint. It emits multi-label candidate scores and explicit abstentions. The Core combines these proposals with immutable Evidence, existing graph state, deterministic validators, and bounded Dependency reconciliation.

The model is a local classifier and candidate proposer. It is not the Context repository, truth authority, temporal database, lineage resolver, or transaction coordinator.

The framework MUST preserve:
- all v0.7 SourceDocument, PageSnapshot, ContentBlock, SourceAnchor, CoverageRecord, Adapter Descriptor and lineage semantics;
- all v0.6 Event, Evidence, Interpretation, Unit/Claim, Entity, Cluster, Relation, Dependency, Question Aspect, Interest, Focus, Observation Frame, temporal, transaction, recovery and convergence semantics;
- all v0.7 invariants I-36 through I-50 and all inherited invariants;
- uncertainty, unknown, partial, opaque, unmapped, and deferred states.

## 3. Resource-bounded Recognition Engine profile

### 3.1 Model profile

A conforming compact profile SHOULD target:
- serialized model artifact: <= 30 MiB (decimal MB MAY be additionally reported);
- quantized, non-autoregressive classification;
- bounded local input window;
- multi-head or multi-label outputs;
- no generative decoding and no KV cache;
- no network access during inference;
- deterministic output for a fixed model, packet, and runtime configuration, subject to documented numeric tolerance.

The profile MAY use hashed character n-grams, byte/subword features, compact embeddings, linear or shallow neural heads, and quantized weights. A large general-purpose tokenizer or generative language model is not required. The exact classifier family is replaceable and MUST be declared in Model Descriptor.

### 3.2 Model responsibilities

The compact model MAY propose:
- document and block type;
- semantic role;
- language/script and domain hints;
- candidate semantic function (assertion, question, definition, instruction, observation, evaluation, quotation, metadata);
- candidate entity/claim/relation/question cues;
- uncertainty, ambiguity, and abstention;
- candidate routing to specialized deterministic processors.

It MUST NOT independently:
- assert source independence or source trust;
- confirm Entity identity or merge/split Clusters;
- resolve contradictions or Question Aspects;
- infer missing table headers, units, dates, speaker attribution, or omitted page content;
- supersede earlier snapshots or claims;
- commit Context State, Focus, Interest, Question status, or Dependency changes.

### 3.3 Core responsibilities

The Core retains authoritative responsibility for typed record creation, evidence linkage, deterministic structural validation, candidate reconciliation, temporal comparison, lineage, Question Aspect lifecycle, Interest/Focus arbitration, Dependency Impact, SCC convergence, transaction validation, commit, history, and recovery.

A compact model score is a producer-specific proposal score, not a calibrated truth probability. Scores from different model versions or heads MUST NOT be compared unless calibration compatibility is declared and validated.

## 4. RecognitionPacket input interface

The Adapter MUST convert raw input into a sequence of bounded packets. A packet contains:
- packet identity and idempotency key;
- source/event/snapshot references and source position;
- block type, semantic role, hierarchy path and structural flags;
- a bounded text slice with exact offsets into immutable source content;
- neighboring block references, not duplicated full documents;
- a compact ContextHint containing active scope, relevant cluster/entity/question IDs, typed relation cues, temporal mode and policy version;
- coverage, extraction confidence, language and adapter capability metadata;
- finalization status and continuation/revision references.

The ContextHint is an index and routing hint, not a substitute for the full graph. It MUST be generated from the committed Context View and carry its state version. Missing or stale hints are explicitly marked; the Core MUST revalidate proposals against the current version.

### 4.1 Bounded window and overlap

The Adapter uses a configurable maximum character/byte window and a small overlap. A semantic unit crossing a packet boundary is represented by continuation references and source offsets. The overlap MUST NOT create duplicate Evidence or duplicate committed Units. The Core performs deterministic deduplication using source identity, offsets, normalized digest, and idempotency keys.

No fixed window size is normative across languages or modalities. Each Model Descriptor declares maximum bytes, maximum characters/tokens if applicable, overlap policy, and truncation behavior. Truncation MUST be explicit and anchored.

### 4.2 Structural-first interface

The Adapter supplies structural facts that do not require semantic inference: heading level, list nesting, table coordinates, header references, code fence/language, quote boundaries, link target, source anchor, timestamps explicitly present in metadata, and capture coverage. The classifier labels the local semantic function but MUST NOT reconstruct structure already available from the Adapter.

For tables, the packet includes cell coordinate, row/column header references, units and footnotes when extracted. If these are unavailable, the cell remains structurally incomplete and cannot support a fully scoped numerical Claim.

### 4.3 ContextHint budget

ContextHint is bounded and typed. It SHOULD include only:
- active scope IDs and current Focus refs;
- a small ranked set of potentially relevant Cluster, Entity, Claim and Question Aspect refs;
- compact relation/dependency signatures;
- temporal scope and current state version;
- retrieval reason for each included reference.

The hint MUST NOT silently discard an unresolved contradiction or a dependency required by policy. If the hint budget is exceeded, Core retains authoritative graph state and records omitted refs; the classifier may abstain or request a wider Core-side lookup.

## 5. Proposal output contract

The model returns a ClassificationProposal with:
- packet_ref, producer/model/runtime versions and base_state_version;
- zero or more typed labels with producer-local scores;
- candidate spans anchored to packet offsets;
- candidate semantic roles and relation cues;
- explicit uncertainty/abstention codes;
- truncation and capability warnings;
- no direct commit operation.

Allowed abstention reasons include LOW_MARGIN, OUT_OF_DISTRIBUTION, UNSUPPORTED_LANGUAGE, WINDOW_TRUNCATED, STRUCTURE_MISSING, CONTEXT_HINT_STALE, LABEL_CONFLICT, and MODEL_ERROR.

A missing prediction is not a negative assertion. A low score is not evidence that a semantic object is absent.

## 6. Two-path recognition

The Core MUST support:
1. Fast path: structural normalization, compact classification, local candidate validation, append-only Evidence registration, and bounded integration.
2. Reconciliation path: triggered only by relevant changes, uncertainty resolution, dependency activation, page revision, late arrival, or explicit policy. It reevaluates the impacted graph, not the entire source corpus.

The fast path MUST NOT bypass v0.7 validation or commit invariants. The reconciliation path MUST retain v0.6 generation, cycle, idempotency, and fixed-point controls.

## 7. RTF and performance contract

### 7.1 Media-stream RTF

For audio/video or another source with a defined presentation duration:
RTF = total measured processing wall time / source presentation duration.
The target is RTF < 0.01 under the declared benchmark profile. Report cold-start and warm steady-state separately, and include adapter, classifier, Core, and end-to-end timings.

### 7.2 Text-only reference RTF

Text has no intrinsic presentation duration. For reproducible text-stream comparison, v0.8 defines a benchmark-only reference clock:
T_ref = normalized Japanese character count / 20 characters per second.
Text RTF = measured end-to-end processing wall time / T_ref.
This is a normalization convention, not a claim about human reading speed. Every report MUST also include characters/second, packets/second, p50/p95/p99 packet latency, and maximum queue depth. Other languages MUST declare a versioned reference rate; their results MUST NOT be silently compared to Japanese results.

### 7.3 Measurement boundary

The report MUST identify CPU model, core/thread count, clock policy, RAM, OS, compiler/runtime, quantization, model artifact size, warm/cold state, thread count, input corpus, packet/window sizes, and whether I/O is included. RTF < 0.01 is an acceptance target, not a presumed result. No design-level or simulated PASS may be reported as measured performance.

### 7.4 Resource accounting

Report separately: model bytes, peak RSS, steady RSS, temporary buffers, adapter memory, Core memory, repository storage, and cache. The model-size target does not cap immutable evidence storage.

## 8. Semantic preservation and failure behavior

v0.8 is conformant only if compact mode preserves the same semantic projection as the reference v0.7 pipeline on equivalent complete inputs, or explicitly records uncertainty/abstention where it cannot safely decide. Silent semantic loss is failure.

Required behavior:
- preserve every source anchor and block boundary;
- retain multiple candidate interpretations where ambiguity exists;
- preserve table headers/units/denominators and quote attribution;
- maintain source lineage and independence counts;
- keep publication, update, retrieval, event and validity times distinct;
- retain Questions and individual Aspects independently;
- keep Interest and Focus separate from truth and Cluster lifecycle;
- trigger bounded Dependency reevaluation on impacted changes;
- retain historical snapshots and reconstructability;
- treat web content as untrusted data;
- never convert model confidence into truth or source trust.

When the compact model cannot classify, the Core MUST use deterministic structural evidence where sufficient, otherwise preserve UNKNOWN/UNMAPPED/OPAQUE/DEFERRED and continue. It MUST NOT invent a semantic answer to meet latency.

## 9. Acceptance and regression

All v0.7 W01-W50 and A-H cases remain mandatory. Add the compact-profile tests C01-C20 in DETAILED_DESIGN_v0.5.md. Acceptance requires:
- semantic projection equivalence or explicitly justified abstention;
- zero invariant violations;
- no duplicate Evidence/Units from overlap;
- deterministic replay and idempotency;
- bounded Impact Set and reconciliation;
- no fabricated structure or provenance;
- measured RTF and latency under the declared profile.

A 50-page thought experiment is not a runtime benchmark. Runtime performance and semantic accuracy require an executable model and Core implementation.

## 10. Compatibility

v0.7 records remain unchanged. v0.8 packet/proposal records are additive. Existing full-text Event payloads MAY be wrapped in a single packet without rewriting their original content. Migration MUST preserve original source offsets and digest. Model replacement creates a new producer/model version; it does not rewrite committed historical interpretations.

## 11. Implementation priorities

1. RecognitionPacket and ClassificationProposal schemas.
2. Structural-first Adapter packetization with exact offsets.
3. Compact Model Descriptor and abstention contract.
4. Core-side ContextHint retrieval and validation.
5. Fast-path batching and bounded reconciliation scheduling.
6. v0.7 corpus plus C01-C20 regression fixtures.
7. Reproducible CPU benchmark harness and semantic projection comparison.

Conclusion: v0.8 preserves the complete v0.7 context semantics while constraining only the local inference interface. A small classifier performs bounded recognition; SURGE Core maintains the continuous, evidence-grounded context.


## 12. Real-time execution and packet scheduling contract

### 12.1 RTF is an end-to-end property

A model artifact of 30 MiB or less does not, by itself, establish any RTF. Model size is a storage measure; RTF depends on CPU architecture, feature extraction, packet size, invocation overhead, batching, memory movement, Core operations, and queue behavior.

For each accepted source range, the implementation MUST measure and report the following separately:

- Adapter and packet construction;
- ContextHint retrieval;
- queue wait and scheduling;
- model preprocessing and inference;
- proposal validation and Evidence integration;
- synchronous Core commit work;
- asynchronous Dependency reconciliation;
- end-to-end settled processing.

The implementation MUST NOT report model-only RTF as end-to-end RTF. If reconciliation is deferred, it MUST report both fast-path latency and settled completion latency, with outstanding work and queue depth.

### 12.2 Two distinct performance objectives

The profile distinguishes:

1. **Streaming stability:** sustained processing throughput MUST exceed incoming source throughput, with bounded queue depth and no silent drops. For media streams, RTF below 1 is the minimum condition for keeping up in a steady state.
2. **Ultra-low-latency target:** end-to-end RTF below 0.01 is the v0.8 optimization target for the declared benchmark profile. This is a 100x processing-speed target relative to the defined source clock, not a guarantee for every packet or device.

RTF is an aggregate ratio and MUST NOT replace packet-latency reporting. A low aggregate RTF can conceal long individual stalls; p50, p95, p99 and maximum packet latency remain mandatory.

### 12.3 Packet formation and micro-batching

The Adapter MUST preserve semantic and structural boundaries while avoiding excessive tiny inference calls.

- Packet boundaries SHOULD prefer paragraph, list item, table row, transcript turn, code statement, or equivalent source-native units.
- A packet MUST remain within the Model Descriptor input budget.
- Adjacent compatible packets MAY be grouped into a micro-batch for one model invocation. Each packet retains its own identity, offsets, digest, ContextHint version and idempotency key.
- Batching MUST NOT concatenate unrelated Context IDs, security scopes, incompatible model versions, or incompatible label schemas.
- Table cells MAY share a batch only when each cell retains its own coordinates and header/unit references.
- Overlap is context only; it MUST NOT duplicate source coverage, Evidence, or committed Units.
- The scheduler MUST flush on a configurable maximum batch size, maximum batch bytes, or maximum wait time. It MUST also flush at explicit finalization, revision, context-switch, or latency-deadline boundaries.
- A batch failure MUST be attributable to individual packets; successful packets MUST NOT be replayed as new Evidence when retrying failed packets.

The implementation SHOULD expose at least two policies: LOW_LATENCY (small batches and short wait bound) and THROUGHPUT (larger batches with a declared wait bound). Neither policy may alter semantic contracts.

### 12.4 Incremental and event-driven Core work

The Core MUST use event-driven invalidation and bounded Impact Sets. It MUST NOT rescan all prior source content for every incoming packet.

- Unchanged packet/source fingerprints SHOULD reuse valid structural normalization and local feature extraction.
- ContextHint retrieval SHOULD use indexed Scope, Focus, source linkage, active Question Aspect, and Dependency edges.
- A state-version change invalidates only hints whose retrieval dependencies are affected; all hints remain subject to commit-time validation.
- Dependency reconciliation SHOULD be scheduled by changed root, exact Impact Set, policy version and effective input fingerprint.
- SCC iteration and reconciliation limits remain mandatory. A limit reached produces STALLED/DEFERRED, never false convergence.
- Caches are accelerators only. Cache eviction MUST NOT delete authoritative Evidence, lineage or historical state.

### 12.5 Backpressure and overload

The input path MUST use bounded queues with explicit capacity and admission policy. When capacity is reached, the Adapter/Core MUST return BACKPRESSURE or DEFERRED with the exact unaccepted source range. It MUST NOT silently drop, reorder, or mark unprocessed ranges complete.

The scheduler SHOULD prioritize causal order within a source and preserve explicit dependency priority across sources. Any priority scheduling MUST be starvation-bounded and observable. Queue depth, oldest-item age, rejected/deferred ranges and retry counts MUST be reported.

### 12.6 CPU execution profile

A benchmark MUST declare:
- CPU model and microarchitecture, physical/logical cores, frequency policy and affinity;
- operating system, compiler, runtime and library versions;
- model architecture, quantization, artifact digest and exact byte size;
- thread count, batch policy, packet size distribution and maximum wait;
- warm-up policy, repetitions, corpus digest and timing boundary;
- memory: model bytes, peak/steady RSS, adapter buffers, Core memory and cache separately.

The baseline SHOULD include a single-thread CPU run to expose intrinsic efficiency, plus a deployment-thread run. Thread oversubscription MUST be avoided and reported.

### 12.7 Japanese text benchmark arithmetic

The benchmark-only Japanese reference clock remains:

T_ref = normalized Japanese character count / 20 characters per second.

Therefore, the RTF < 0.01 target implies the following maximum end-to-end processing budgets:

| Normalized characters | T_ref | Maximum wall time for RTF < 0.01 |
|---:|---:|---:|
| 20 | 1 s | 10 ms |
| 50 | 2.5 s | 25 ms |
| 100 | 5 s | 50 ms |
| 200 | 10 s | 100 ms |
| 1,000 | 50 s | 500 ms |

These are arithmetic consequences of the benchmark convention, not measured model performance or human reading-speed claims. Small packets are particularly sensitive to fixed invocation and scheduling overhead. Implementations MUST report packet-size-stratified latency and RTF; they MUST NOT infer small-packet performance from large-batch throughput.

A hypothetical 9.4 ms inference time, if ever used in an example, is illustrative only. It MUST NOT be presented as a SURGE benchmark or as evidence that a 30 MiB model achieves that latency.

### 12.8 Performance result classes

Every result MUST be labeled as one of:
- DESIGN_TARGET: specified but not measured;
- ARITHMETIC_EXAMPLE: derived from a stated formula, not measured;
- MODEL_ONLY_MEASURED: measured inference only;
- FAST_PATH_MEASURED: measured through synchronous packet acceptance/commit boundary;
- END_TO_END_MEASURED: includes all work required by the declared completion definition;
- SETTLED_MEASURED: includes deferred reconciliation through a stable settled state.

Only END_TO_END_MEASURED or SETTLED_MEASURED can substantiate the v0.8 end-to-end RTF target. Reports MUST include semantic coverage/abstention rate alongside speed so that excessive abstention cannot masquerade as successful acceleration.

## 13. Revised acceptance gates

The following gates are independent and MUST be reported separately:

| Gate | Acceptance condition |
|---|---|
| Artifact | model artifact <= 30 MiB, exact digest and byte count recorded |
| Semantic | v0.7 semantic projection is equivalent, or uncertainty is explicitly preserved |
| Safety | zero violations of inherited and v0.8 invariants |
| Throughput | sustained input rate exceeds source rate without unbounded queue growth |
| Latency | p50/p95/p99/max packet latency reported under declared packet-size strata |
| RTF | end-to-end RTF < 0.01 measured under a declared profile |
| Coverage | classification coverage, abstention, deferred and unsupported rates reported |
| Memory | peak and steady RSS plus component-wise memory accounting reported |

Passing one gate MUST NOT be described as passing another. In particular, artifact size does not prove speed, speed does not prove semantic parity, and design-level thought experiments do not prove either.

## 14. Revised implementation order

1. Implement deterministic structural packetization and exact source anchors.
2. Implement bounded queues, backpressure, per-packet idempotency and micro-batch scheduling.
3. Implement ContextHint retrieval and stale-version revalidation.
4. Implement compact classifier and explicit abstention.
5. Implement event-driven Core integration and bounded Dependency reconciliation.
6. Add packet-size-stratified benchmark harness with complete timing boundaries.
7. Compare compact and reference v0.7 semantic projections on W01-W50, A-H and C01-C20.
8. Optimize measured bottlenecks; do not optimize model inference in isolation when Adapter/Core overhead dominates.

## 15. v0.8 normative conclusion

The 30 MiB model profile is plausible as a bounded local recognizer, but the specification makes no empirical claim that a particular model, CPU, or implementation achieves RTF < 0.01. The design targets this through structural-first input, bounded packets, micro-batching, indexed ContextHint retrieval, incremental Core updates, and event-driven reconciliation. Full v0.7 semantic state remains authoritative in the Core. The acceptance decision is made only from reproducible end-to-end measurements and semantic regression results.


## 16. Cross-packet semantic scope contract

### 16.1 Scope-bearing spans

A semantic cue whose interpretation may extend beyond one RecognitionPacket MUST be represented by a Core-owned ScopeSpan record. A ScopeSpan contains:
- scope_span_id and scope_kind (NEGATION, CONDITION, MODALITY, CONCESSION, CONTRAST, COREFERENCE, ANAPHORA, QUOTATION, TEMPORAL, LIST_CONTINUATION, TABLE_HEADER);
- source_anchor_refs and exact normalized-source offsets;
- opener_ref, optional closer_ref, and continuation_status (OPEN, CLOSED, UNRESOLVED, INVALIDATED);
- antecedent/candidate refs where applicable;
- origin packet refs and source-order range;
- confidence/proposal provenance, which remains producer-local;
- resolution_policy_version.

The compact model may propose local cues and candidate boundaries. Only Core may extend, close, split, or invalidate a ScopeSpan after checking source order, offsets, structural boundaries and available evidence.

### 16.2 Boundary continuation

A packet ending while a scope is open MUST emit a continuation reference, not a completed semantic interpretation. The next packet may close or refine that scope. Until closure or an explicit policy-defined safe boundary, dependent Claim candidates MUST retain the relevant uncertainty. Packet completion does not imply semantic-scope completion.

A packet may reference an earlier packet without copying its text. Core retrieves the exact source slice from the immutable source representation. Missing antecedents remain UNRESOLVED; the system MUST NOT guess them from a truncated window.

### 16.3 Scope propagation

Scope propagation follows source order and typed rules, not generic text similarity. A negation scope, for example, may govern a predicate span but MUST NOT automatically negate every later sentence. Conditions and concessions retain their own target spans. Quote scope keeps attributed content separate from author commentary. ScopeSpan records are immutable revisions: corrections create new records with revision lineage.

## 17. Causal scheduling and ordered integration

### 17.1 Three distinct orders

The implementation MUST distinguish:
1. source order: order in which content occurs in its source;
2. inference completion order: order in which parallel model jobs finish;
3. Core commit order: order in which accepted semantic deltas become visible.

Inference completion order MUST NOT define source meaning or commit precedence.

### 17.2 Per-source sequencer

Each source stream has a monotonic source_sequence and a sequencer watermark. The scheduler may run compatible packets concurrently, but Core integration is serialized by source causal order unless a declared commutativity proof permits reordering. A later packet whose predecessor is not yet integrated remains READY_BUT_BLOCKED. Independent sources may progress concurrently.

A source sequence gap is explicit. The system MUST NOT advance the watermark across an unaccepted, deferred, failed, or unknown range. A gap may be closed only by accepted packet receipt, an explicit source correction/retraction event, or an authorized gap-resolution record with provenance.

### 17.3 Proposal and Delta binding

Every proposal records packet_ref, source_sequence, source_range, base_state_version, ContextHint state_version, model descriptor, and idempotency key. Core validates the proposal against the current committed state and exact predecessor watermark before generating or committing a Delta.

When a stale proposal can be safely rebased, Core MUST regenerate/revalidate its Delta against the current version. It MUST NOT merely replace the version field. A conflict affecting its read set returns NEEDS_REBASE or DEFERRED.

### 17.4 Deterministic replay

Given the same immutable source records, model/runtime descriptor, policy versions, and deterministic tie-break rules, replay MUST produce the same ordered integration decisions or explicit recorded nondeterminism. A late-arriving earlier source segment triggers bounded re-evaluation of affected later records; it does not rewrite their historical commit records.

## 18. ContextHint overflow and mandatory retrieval

### 18.1 Mandatory versus optional references

ContextHint candidate references are divided into:
- MANDATORY: unresolved Question Aspects, active Dependencies whose trigger condition is met, open ScopeSpans crossing the packet boundary, required source/lineage refs, and policy-mandated Focus/Scope refs;
- OPTIONAL: ranked related Clusters, historical examples, low-priority entity candidates and convenience context.

MANDATORY references are not evictable by OPTIONAL references. If mandatory references exceed the packet/model hint budget, Core MUST return a truncated hint with omitted mandatory count and a retrieval ticket. The classifier MUST abstain from decisions that require omitted references.

### 18.2 Retrieval ticket

A RetrievalTicket is a Core-owned, immutable query descriptor containing ticket_id, context_id, state_version, packet_ref/source_range, omitted_ref_count, omitted_ref_digest, mandatory_reason_codes, retrieval_policy_version and expiry/revalidation policy. It is not a copy of the omitted graph.

The proposal may return NEEDS_CORE_LOOKUP with ticket refs and the exact label/span needing context. Core resolves the ticket against authoritative state, records the retrieved refs and reasons, and revalidates the candidate. Tickets are idempotent and state-version-bound.

### 18.3 Overflow behavior

If the mandatory set cannot fit:
1. preserve all mandatory refs in Core state;
2. include the highest-priority subset in the bounded hint;
3. record the complete omitted set by digest and retrievable query;
4. mark affected decisions CONTEXT_INCOMPLETE;
5. abstain or defer any decision whose validity depends on omitted refs;
6. schedule lookup/reconciliation only for those decisions.

No context item is deleted merely because it was omitted from ContextHint. A hint is never the authoritative store.

## 19. Quality, coverage and performance acceptance

RTF is accepted only alongside semantic quality. A benchmark report MUST include:
- per-label precision, recall and F1 (or an explicitly justified task-specific metric);
- candidate coverage and abstention rate;
- selective risk at declared coverage levels;
- abstention rate on examples classifiable by the reference pipeline;
- false-negative rate for negation, condition, modality, attribution and temporal scope;
- semantic projection EXACT/EQUIVALENT rate, and explicit INCOMPARABLE/CONFLICTING/DEFERRED counts;
- duplicate Evidence/Unit count (required zero);
- queue stability, source-order violations (required zero), and p50/p95/p99/max latency;
- end-to-end fast-path and settled RTF, separately;
- peak/steady RSS and model/runtime/adapter/Core memory accounting.

A model cannot pass by abstaining on nearly all inputs. Each benchmark declares minimum coverage and maximum selective risk before execution. These thresholds are profile-specific and versioned; they MUST be fixed before the test run, not tuned after seeing results.

A semantic acceptance result and a performance acceptance result are independent gates. Overall compact-profile acceptance requires both gates, zero invariant violations, and no silent source-range loss. Thought experiments remain DESIGN_REVIEW, never MEASURED_PASS.

## 20. New v0.8 invariants

| ID | Invariant |
|---|---|
| I-61 | Scope Continuity: semantic scopes crossing packet boundaries retain explicit continuation and exact source anchors |
| I-62 | Scope Non-Overreach: scope propagation is typed and bounded; no blanket carry-forward |
| I-63 | Causal Integration: inference completion order cannot override source causal order |
| I-64 | Watermark Integrity: source watermarks cannot advance across unresolved gaps |
| I-65 | Mandatory Hint Retention: mandatory graph references cannot be discarded by ContextHint truncation |
| I-66 | Lookup Completeness: omitted mandatory refs remain retrievable through a version-bound Core ticket |
| I-67 | Abstention Validity: decisions depending on omitted context must abstain or defer |
| I-68 | Quality-Performance Coupling: RTF acceptance requires predeclared coverage and selective-risk gates |
| I-69 | Replay Determinism: ordered integration is deterministic or explicitly records nondeterminism |
| I-70 | Scope Provenance: every ScopeSpan revision remains traceable to immutable source anchors and proposal lineage |

## 21. Conformance additions

Add detailed-design cases D01-D12 and API rules in CORE_API_v0.3.md. Existing W01-W50, A-H, C01-C20 and T-001–T-101 remain inherited mandatory fixtures. A passing thought experiment does not establish executable conformance, model accuracy, memory use, or RTF.
