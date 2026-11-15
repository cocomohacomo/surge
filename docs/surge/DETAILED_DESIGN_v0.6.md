# SURGE 詳細設計 v0.6
## v0.9 Closed Contracts, Algorithms and Verification

Version: 0.6.0
Normative for: SPECIFICATION_v0.9.md
Parent: DETAILED_DESIGN_v0.5.md

## 1. Canonical source range

Canonical range key:
(context_id, source_id, source_generation, source_sequence, offset_basis, start_offset, end_offset, source_range_digest).

source_range_digest is SHA-256 over the exact normalized UTF-8 byte slice [start,end), using the declared offset_basis normalization version. Packet overlap bytes outside this half-open semantic range are not included. Canonical request digest additionally includes packet schema version, source refs, structural metadata digest, context ID and idempotency operation family.

Decision table:
- same canonical range key + same canonical request digest: return original packet/Evidence refs, status DUPLICATE;
- same range identity + different source_range_digest: RANGE_CONTENT_CONFLICT;
- same range and content digest + different non-content metadata: METADATA_CONFLICT unless an allowed metadata revision is explicitly recorded;
- same idempotency key + different canonical request digest: IDEMPOTENCY_KEY_REUSED;
- changed source bytes with valid source_revision_sequence and revision_of_ref: append revision, preserve old range/Evidence and calculate bounded Impact Set;
- changed bytes without revision lineage: reject.

## 2. Gap detector and resolver

Maintain per (context, source, generation):
next_expected_sequence, committed_watermark, observed_sequences, gap_records, authorized_skip_records.

On packet sequence s:
1. validate generation and immutable sequence assignment;
2. if s > next_expected_sequence, create OPEN gaps for missing sequence interval;
3. accept packet as READY or READY_BUT_BLOCKED;
4. if s is an already-resolved gap, append late-arrival event and set gap REOPENED; never delete skip audit;
5. commit only when all predecessors are committed or terminal AUTHORIZED_SKIP;
6. recompute watermark as max contiguous terminal sequence from prior watermark+1.

Authorize skip request fields: context_id, source_id, source_generation, sequence, reason_code, reason_text, evidence_refs, actor_ref, authorization_scope, policy_version, issued_at, idempotency_key. All are mandatory except evidence_refs may be empty only when policy reason explicitly permits administrative source-loss declaration. actor must possess SOURCE_GAP_SKIP capability for the Context. Policy version must be current. A skip cannot be issued for an observed/committed packet or outside retention window. Repeated identical request returns same audit record; changed request under same key is IDEMPOTENCY_KEY_REUSED.

Allowed reason codes: SOURCE_CONFIRMED_LOSS, SOURCE_WITHDRAWN, RETENTION_EXPIRED, ADMINISTRATIVE_RECOVERY. Free text alone is not a reason code. Invalid authorization => GAP_SKIP_UNAUTHORIZED. Missing reason/evidence required by policy => GAP_SKIP_AUDIT_INCOMPLETE. Existing non-gap => SOURCE_SEQUENCE_NOT_GAP.

Late packet after skip:
- retain immutable packet and Evidence;
- append LATE_AFTER_SKIP event referencing skip audit;
- mark gap REOPENED for reconciliation status while retaining historical skip decision;
- recompute affected source-order-dependent proposals;
- do not silently roll back later commits; emit compensating Delta if required.

## 3. Causal commit algorithm

A packet is eligible iff:
(a) source generation is accepted;
(b) all lower sequences through predecessor set are COMMITTED or AUTHORIZED_SKIP;
(c) source range and revision lineage validate;
(d) proposal base state can be rebased without read/write conflict;
(e) ContextHint/tickets are revalidated;
(f) all ScopeSpan atomicity checks pass.

Inference completion sets proposal status PROPOSED only. If (a)-(f) fail because predecessor pending, status READY_BUT_BLOCKED. If gap exists, status BLOCKED_BY_GAP with gap refs. If state conflict, NEEDS_REBASE. Never change a proposal's base version without an append-only rebase record containing old/new version, read set, write set, decision and validator version.

Deterministic tie-break for same sequence: source_revision_sequence, source-native block order, adapter stable subrange order, packet ID lexical. Tie-break only orders integration; it does not resolve semantic ambiguity.

## 4. ScopeSpan validator

All offsets are half-open and interpreted only in declared offset_basis. Validate source digest, generation, source range, anchor existence, opener/closer ordering, target binding and packet lineage.

Type-specific closure:
- NEGATION: exact negation cue + compatible predicate anchor; no propagation to a second independent predicate.
- CONDITION: condition anchor and consequent anchor both required; until both exist, no unconditional Claim.
- MODALITY: modality cue and proposition target required; store modality kind separately from Claim polarity and factuality.
- CONCESSION/CONTRAST: both proposition refs and relation direction required; preserve both sides.
- COREFERENCE/ANAPHORA: antecedent candidate set and ambiguity status required; Entity merge is a separate Core operation.
- QUOTATION: opening/closing quote anchors and attribution ref or explicit UNKNOWN attribution; author stance remains separate.
- TEMPORAL: time cue, target proposition and temporal role required.
- LIST_CONTINUATION/TABLE_HEADER: exact parent/header ref required; missing ref yields OPAQUE/UNMAPPED.

If multiple valid parses remain, store candidate bindings and status UNRESOLVED. A ScopeSpan cannot be CLOSED with an empty target set except policy-declared marker-only types. Closure, supporting Evidence refs and affected Claim/Relation update commit atomically.

## 5. ContextHint and RetrievalTicket algorithm

Canonicalize refs by immutable record ID; reject foreign-Context refs. Mandatory priority is fixed by SPEC v0.9. Sort within each class by causal relevance (source overlap, active dependency depth, then stable ID). Never use producer score to evict mandatory refs.

Ticket key: (context_id, packet_ref, source_range_digest, omitted_ref_digest, retrieval_policy_version). Ticket query stores predicate, omitted digest/count, mandatory reason counts, source scope, initial state version, expiry and authorization scope.

On resolve:
1. verify Context authorization and ticket status;
2. compare current state version with ticket version;
3. if changed, re-evaluate stored predicate at current version; if query policy cannot be replayed, return STALE;
4. return exact current refs, retrieval reasons, completeness and resolved version;
5. mark RESOLVED or STALE idempotently.
Ticket read never mutates graph state. Any new candidate uses normal proposal validation. Repeated identical resolve returns same logical result for same state version; a changed state triggers a new resolution revision.

## 6. Benchmark and metric definitions

For dataset E of eligible examples:
coverage = |accepted examples| / |E|.
abstention_rate = |abstained eligible examples| / |E|.
per-label coverage_l = accepted examples for label l / eligible examples for l.
recall_l = TP_l / (TP_l + FN_l).
selective_risk(c) = errors among accepted examples at operating point c / accepted examples at c.
semantic_agreement = (EXACT + EQUIVALENT) / comparable examples.
critical_FNR = FN_critical / (TP_critical + FN_critical).

All denominators and exclusions are emitted in report. If denominator=0 or below profile.min_examples_per_critical_label, status INCONCLUSIVE. A profile may define minimum sample counts; it cannot waive zero-denominator behavior. Abstained examples are excluded from accepted prediction risk but remain in coverage denominator and are separately reported. If an abstention conceals a critical false negative, reference annotation still determines FN under the benchmark's declared policy.

Threshold comparisons are exact decimal/rational comparisons; rounding is for display only. Boundary equality passes when threshold uses >= or <= as specified. Missing metric is INCONCLUSIVE. Any invariant violation or duplicate Evidence/Unit is immediate semantic FAIL.

Benchmark registration stores immutable profile JSON, profile digest, corpus/split digests, creation time and evaluator identity. Any changed threshold, corpus, split or metric implementation creates a new profile version and ID. Post-result mutation is rejected.

## 7. D01-D20 verification matrix

D01-D12 exact packet examples, offsets and expected assertions are in tests/conformance/SURGE_v0.9_D01_D12.json. D13-D20 are in SURGE_v0.9_D13_D20.json. Expected states are canonical enums and expected_counts are machine assertions. All examples declare UTF-8 byte offsets and SHA-256 over exact source ranges; fixture generator must compute digests rather than copy illustrative placeholders.

| ID | Added assertion |
|---|---|
| D13 | authorized skip has actor, capability, reason, policy and audit; watermark advances only across contiguous terminal seq |
| D14 | missing capability/reason/evidence requirement rejects; watermark unchanged |
| D15 | late packet is retained; skip audit remains; gap becomes REOPENED and bounded reconciliation scheduled |
| D16 | same range/digest/request retry returns same Evidence refs and creates no new Evidence |
| D17 | same range identity with different digest returns RANGE_CONTENT_CONFLICT |
| D18 | explicit revision appends new snapshot/Evidence and preserves old record |
| D19 | zero critical-label denominator is INCONCLUSIVE, never PASS |
| D20 | benchmark threshold/corpus mutation after registration is rejected |

## 8. Failure injection and replay

Every state-changing operation is tested at: before write, after append but before commit marker, after commit marker before response, and response-loss retry. Atomic transaction exposes either complete Delta or no committed change. Recovery replays append-only audit and verifies identical state digest. Nondeterministic external inference is stored as immutable proposal; replay reuses proposal, not model re-execution, unless a new producer version is explicitly requested.

## 9. Residual empirical obligations

Thought experiments can close contract ambiguity but cannot prove model accuracy, CPU timing, memory, database isolation, crash durability or queue stability. Those are measured only by executable tests on declared implementation/runtime/hardware.
