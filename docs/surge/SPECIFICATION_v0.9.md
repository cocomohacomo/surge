# SURGE 基礎設計 v0.9
## Universal Context Cognition Framework
### Closed Execution Contracts and Conformance-Ready Recognition

Version: 0.9.0
Status: Normative architecture and Core contract specification
Parent: SPECIFICATION_v0.8.md
Detailed design: DETAILED_DESIGN_v0.6.md
Core API: CORE_API_v0.4.md
Input schema: schemas/surge-v0.9-input.schema.json
Proposal schema: schemas/surge-v0.9-proposal.schema.json
Benchmark profile schema: schemas/surge-v0.9-benchmark-profile.schema.json

## 1. Version policy and scope

v0.9 is an additive closure of v0.8 execution contracts. It preserves v0.6 cognition and transaction semantics, v0.7 heterogeneous-source semantics, and v0.8 bounded recognition, ScopeSpan, source sequencing, ContextHint and RetrievalTicket concepts. It does not claim a production implementation or measured performance.

The v0.8 document remains historical and immutable. New conforming implementations target v0.9. Existing v0.8 records are readable under an explicit compatibility adapter; absent v0.9 fields are UNKNOWN, never inferred.

## 2. Normative principles

1. Source content and Evidence are immutable; correction is append-only and lineage-linked.
2. Recognition proposals are untrusted candidates. Only Core may validate and commit state.
3. A semantic scope may cross packet boundaries. Packet completion is not scope completion.
4. Inference may run in parallel; integration follows source causality.
5. A missing source sequence is a gap, not empty content.
6. ContextHint is a bounded index, never authoritative state. Mandatory omissions remain retrievable.
7. A stale RetrievalTicket is re-evaluated or rejected, never silently reused.
8. Duplicate delivery is idempotent. Conflicting content for an occupied source range is not a duplicate.
9. Semantic quality and performance are independent gates; overall acceptance requires both.
10. Unknown, partial, opaque, unmapped, deferred and abstained states are first-class outcomes.

## 3. Closed contracts

### 3.1 Source identity and range identity

A source stream is identified by (context_id, source_id, source_generation). Within one generation, source_sequence is a non-negative, monotonically assigned integer and is never reused. A source range is identified by immutable source digest, offset_basis and half-open [start,end) offsets. Every packet carries source_range_digest, which is the digest of the exact normalized bytes covered by that range, not of overlapping context bytes.

A source correction that changes bytes MUST use a new source_revision_sequence and append-only revision lineage. It MUST NOT overwrite a committed range. A retry with the same generation, sequence, range, digest and canonical idempotency request is DUPLICATE. The same identity/range with a different digest is RANGE_CONTENT_CONFLICT unless a valid revision record explicitly links it as a correction. The same idempotency key with a different canonical request is IDEMPOTENCY_KEY_REUSED.

### 3.2 Gap lifecycle

Gap states: OPEN, RESOLVED_BY_PACKET, AUTHORIZED_SKIP, REOPENED, INVALIDATED.
- OPEN is created for each missing sequence once a later sequence is observed.
- RESOLVED_BY_PACKET requires the missing packet and validated range/digest.
- AUTHORIZED_SKIP requires an explicit authorized actor, reason code, evidence/reference, policy version, timestamp and idempotency key. It records absence as an administrative decision, not as source content.
- REOPENED occurs when a late packet arrives after an authorized skip. The late packet is retained; Core computes a bounded Impact Set and does not rewrite the historical skip.
- INVALIDATED is reserved for a source-generation correction with explicit lineage.

The committed watermark is the greatest contiguous sequence whose packets are committed or whose gaps have authorized terminal skip records. A gap resolution never fabricates Evidence. Unauthorized skip, empty reason, missing audit provenance or skip of a non-gap is rejected.

### 3.3 ScopeSpan closure

ScopeSpan is Core-owned, append-only and typed: NEGATION, CONDITION, MODALITY, CONCESSION, CONTRAST, COREFERENCE, ANAPHORA, QUOTATION, TEMPORAL, LIST_CONTINUATION, TABLE_HEADER.
Each span records source identity, exact anchors and offsets, offset_basis, source digest, opener, optional closer, target/antecedent refs, packet refs, producer lineage, policy version, status and revision lineage.

Allowed transitions:
- OPEN -> CLOSED only when a policy-valid closer and all required target bindings are anchored.
- OPEN -> UNRESOLVED when the stream boundary or ambiguity prevents closure.
- OPEN/CLOSED/UNRESOLVED -> INVALIDATED only by a new append-only revision with invalidation basis.
- UNRESOLVED -> CLOSED when later evidence resolves the exact pending binding.
No packet FINAL marker closes a scope unless the scope policy explicitly defines source-end closure. Negation does not propagate to independent later predicates. Conditional consequents are not unconditional facts. Modality is not factuality. Quote stance is not author stance. Ambiguous antecedents do not authorize Entity merge.

### 3.4 Causal sequencer

Per source generation, Core maintains next_expected_sequence, committed_watermark, gap_set and packet states. Inference completion order is independent of commit order. A proposal for N+2 completed before N remains READY_BUT_BLOCKED. Commit eligibility requires all causal predecessors committed, explicitly authorized-skipped, or covered by a validated commutativity proof.

A commutativity proof MUST name the operations, read/write sets, policy version and verifier. It is not inferred from different packet IDs. Cross-source streams have no invented global total order; explicit dependencies and transaction read/write validation govern them.

### 3.5 ContextHint and RetrievalTicket

Mandatory refs, in fixed priority order:
1. open ScopeSpan continuations intersecting the packet;
2. triggered required Question Aspects;
3. triggered Dependencies in the exact Impact Set;
4. source/revision/lineage refs required for provenance;
5. active Focus and Scope policy refs.

Optional refs are added only after all mandatory refs fit. Refs are canonicalized and deduplicated before budgeting. If mandatory refs exceed the budget, include the deterministic prefix that fits, set context_complete=false, record included/omitted digests and counts, and issue one idempotent RetrievalTicket per (context, packet, range, omitted_digest, policy_version).

Ticket states: ISSUED, RESOLVED, STALE, EXPIRED, SUPERSEDED. Resolution is read-only and returns exact refs, reasons, completeness and resolved_state_version. If current state differs from ticket state, Core MUST re-run the stored query against current state or return STALE. Ticket resolution does not commit a semantic proposal. Any resulting proposal undergoes ordinary validation and commit. Ticket access is Context-scoped and authorization-checked.

### 3.6 Quality gate profile

A Benchmark Profile MUST be immutable and registered before evaluation. It binds corpus digest, split digest, reference implementation/version, label taxonomy, critical label set, threshold values, metric implementation version, model/runtime descriptors and evaluation protocol.

Mandatory threshold fields:
- minimum overall coverage;
- minimum per-critical-label coverage;
- minimum per-critical-label recall;
- maximum selective risk at each declared coverage point;
- minimum semantic projection agreement;
- maximum critical false-negative rate;
- maximum invariant violations (MUST be zero);
- maximum duplicate Evidence and Unit counts (MUST be zero).

Thresholds are numeric fractions in [0,1], sample minima are positive integers, and every critical label must have a non-zero eligible denominator. If a denominator is zero, result is INCONCLUSIVE, not PASS. Thresholds cannot be modified after results are observed; a changed threshold requires a new profile ID and digest.

Coverage = accepted non-abstained eligible examples / all eligible examples.
Selective risk = errors among accepted examples / accepted examples at the declared coverage operating point.
Abstention is not counted as a correct prediction. Report both coverage and selective risk. A system that abstains on almost all examples fails minimum coverage even if accepted predictions are accurate.

Gate results: PASS, FAIL, INCONCLUSIVE. Semantic gate and performance gate are evaluated independently. Overall PASS requires both PASS and zero invariant violations. Missing metrics, invalid profile, insufficient denominators or unmeasured requirements yield INCONCLUSIVE, never PASS.

### 3.7 Performance measurement

Model artifact size, peak/steady RSS, adapter memory, Core memory, cache and repository storage are reported separately. RTF is end-to-end and includes packetization, ContextHint, queue wait, inference, validation, commit and settled reconciliation. Report fast-path and settled latency/RTF separately. Include p50/p95/p99/max latency, throughput, queue depth, oldest-item age and deferred ranges. Model-only RTF MUST NOT be labeled end-to-end RTF.

### 3.8 Claim correction and retraction lifecycle

Claim content and Evidence references are immutable after commit. A correction creates a new Evidence-backed Claim and an append-only relation `new_claim --SUPERSEDES--> old_claim`. An explicit withdrawal uses `evidence --RETRACTS--> claim`, where the Evidence records the basis for withdrawal.

Claim lifecycle states are ACTIVE, SUPERSEDED and RETRACTED. New Claims start ACTIVE. SUPERSEDES requires both Claims to be ACTIVE and transitions only the target to SUPERSEDED. RETRACTS requires an Evidence source and ACTIVE Claim target, then transitions the target to RETRACTED. Any attempt to supersede or retract a non-ACTIVE Claim is rejected. These relation and status changes are part of the same SemanticDelta transaction and are reconstructed from Journal replay.

Historical Claims remain retrievable with their original Evidence and a `claim_status` field. Current-state consumers must explicitly filter ACTIVE; audit consumers retain all lifecycle states. See CLAIM_LIFECYCLE_v0.1.md.

### 3.10 Temporal dependency propagation

Dependency predicates may explicitly route a changed Claim according to its
latest valid-time state at a supplied RFC3339 instant:
`CLAIM_TEMPORAL_STATE:<STATE>@<RFC3339>`. Supported states are UNKNOWN,
NOT_YET_VALID, VALID and EXPIRED. A predicate is evaluated only when its
prerequisite Claim is included in the current changed-node set. A validity
revision and its resulting dependency evaluation are persisted atomically.
The Core MUST NOT infer wall-clock time or silently treat missing validity as
VALID. Temporal predicates on non-Claim prerequisites and malformed values
fail closed. This mechanism routes impacted records; it does not automatically
rewrite dependent Claims. See DEPENDENCY_ENGINE_v0.1.md.

### 3.9 Claim temporal validity

Claim lifecycle and temporal validity are independent dimensions. Lifecycle states (ACTIVE, SUPERSEDED, RETRACTED) describe graph assertion status; valid time describes the interval in the modeled world. Each Claim may have append-only validity revisions with a half-open interval `[valid_from, valid_until)`, an Evidence basis, revision number and `recorded_state_version`.

Timestamps MUST be RFC3339 with an explicit timezone. Core compares normalized instants and rejects invalid dates, malformed offsets, timezone-free values and non-positive intervals. Empty `valid_until` means open-ended. Revision numbers are contiguous per Claim. A corrected interval appends a new revision and preserves the prior one.

Point-in-time evaluation returns UNKNOWN, NOT_YET_VALID, VALID or EXPIRED. Missing validity is UNKNOWN, never implicitly current. Transaction time is represented by the state version at which each revision was committed. See CLAIM_TEMPORAL_VALIDITY_v0.1.md.

## 4. Invariants

Inherit all applicable invariants I-01 through I-70. v0.9 adds:
- I-71 Range Digest Integrity: a source range identity binds exact bytes, offset basis and source generation.
- I-72 Revision Append-Only: changed bytes require revision lineage; committed Evidence is never overwritten.
- I-73 Gap Authorization: a skipped sequence requires explicit authorized audit record.
- I-74 Watermark Contiguity: watermark advances only over committed packets or authorized terminal gaps.
- I-75 Late Arrival Preservation: a packet arriving after skip is retained and triggers bounded reevaluation.
- I-76 Scope Closure Proof: CLOSED requires policy-valid anchored closure and target binding.
- I-77 Ticket Revalidation: state-version changes force re-evaluation or STALE.
- I-78 Benchmark Immutability: profile and corpus digests are fixed before evaluation.
- I-79 Denominator Validity: zero or insufficient denominators cannot pass a metric gate.
- I-80 Abstention Coverage: abstentions reduce coverage and cannot count as correct predictions.
- I-81 Dual-Gate Acceptance: overall conformance requires semantic and performance gates.
- I-82 Conflict Classification: same range with changed digest is conflict/revision, never duplicate.
- I-83 Audit Completeness: gap skips, revisions, ticket resolution and rebase retain actor/policy/time lineage.\n- I-84 Claim History Preservation: correction and withdrawal never overwrite or delete a committed Claim or its Evidence; lifecycle changes are append-only and replay-deterministic.\n- I-85 Temporal Validity Separation: valid time and transaction time are stored separately; missing timezone or validity data is never inferred, and interval revisions preserve prior assertions.

## 5. Conformance suite

The mandatory regression set is T-001–T-101, W01–W50, A–H, C01–C20 and D01–D12. v0.9 adds executable-oriented specifications D13–D20:
- D13 authorized gap skip and watermark advancement;
- D14 unauthorized/malformed gap skip rejection;
- D15 late packet after authorized skip;
- D16 same range/same digest idempotent retry;
- D17 same range/different digest conflict;
- D18 explicit source revision append-only behavior;
- D19 zero-denominator quality gate INCONCLUSIVE;
- D20 immutable benchmark profile / threshold tampering rejection.

Every case declares input, preconditions, operation order, expected state, Delta, invariant checks, errors and replay behavior in tests/conformance/SURGE_v0.9_D13_D20.json. D01–D12 executable-oriented definitions are in SURGE_v0.9_D01_D12.json.

Claim lifecycle conformance CL01–CL06 and temporal validity conformance TV01–TV06 are exercised by the ContextGraph integration suite: initial ACTIVE state, supersession, duplicate supersession rejection, evidence-backed retraction, full-Delta atomic rejection and restart reconstruction.\n\nA design review is DESIGN_REVIEW only. Schema validation, unit/integration execution, model accuracy and runtime measurement are separate evidence classes. Never aggregate thought-experiment PASS into executable PASS.

## 6. Compatibility and migration

v0.8 packet/proposal payloads are accepted only through an explicit compatibility profile. Missing source_range_digest is computed only when the exact immutable source bytes and offset_basis are available; otherwise the range is LEGACY_UNVERIFIED and cannot participate in conflict-free deduplication. Missing source_generation or source_sequence is LEGACY_UNORDERED and cannot advance the v0.9 watermark. No default sequence or digest is fabricated.

Migration is append-only and reports LEGACY_UNVERIFIED, LEGACY_UNORDERED and unresolved lineage counts. Existing historical records are not rewritten.

## 7. Implementation sequence

Phase A: schemas, canonical hashing, fixture validator, profile registry and golden test harness.
Phase B: source sequencer, gap audit operations, range conflict/revision handling.
Phase C: ScopeSpan validation/state machine and atomic Delta integration.
Phase D: ContextHint mandatory budgeting and RetrievalTicket revalidation.
Phase E: quality metric runner, semantic projection comparator and dual-gate report.
Phase F: deterministic replay, concurrency, fault injection and performance benchmarks.

Implementation MUST start only after the v0.9 contracts and fixtures are reviewed and committed. Passing design cases does not authorize a claim of implementation conformance.

## 8. Version decision

v0.9 closes the specification-level ambiguity found in v0.8 for gap resolution, range conflicts, quality denominators, benchmark immutability, ticket staleness and typed scope closure. It does not claim empirical closure of model accuracy, runtime, memory or production reliability; those require implementation evidence.
