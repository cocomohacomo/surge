# SURGE Core API v0.4
## v0.9 Closed Source, Scope, Ticket and Benchmark Contracts

Version: 0.4.0
Applies to: v0.6 base API + v0.8 packet API + v0.9 closed contracts
Normative transport-neutral API.

## 1. Authority and common rules

Only Core Transaction Coordinator commits Context State. Adapters and Recognition Engines submit immutable packets/proposals only. All mutating operations require Context-scoped idempotency keys. Repeating the same canonical request returns the original logical result; reusing a key for a different request returns IDEMPOTENCY_KEY_REUSED. All records are append-only. Timestamps are RFC3339 with explicit timezone.

## 2. Source packet operations

### ingest_packet(request)
Required: context_id, packet(v0.9 RecognitionPacket), idempotency_key.
Returns: packet_ref, accepted_range_ref, duplicate, packet_status, source_sequence, committed_watermark, gap_refs.
Errors: PACKET_SCHEMA_INVALID, PACKET_OFFSET_INVALID, PACKET_DIGEST_MISMATCH, RANGE_CONTENT_CONFLICT, METADATA_CONFLICT, SOURCE_SEQUENCE_REUSED, SOURCE_GENERATION_MISMATCH, IDEMPOTENCY_KEY_REUSED.

Same range+digest+canonical request is duplicate. Same range identity with changed digest is conflict unless explicit revision lineage validates. Overlap context bytes do not create new Evidence.

### get_source_status(context_id, source_id, source_generation)
Returns next_expected_sequence, committed_watermark, gap records, blocked packets and source revision lineage.

### authorize_gap_skip(request)
Required: context_id, source_id, source_generation, sequence, reason_code, reason_text, evidence_refs (policy-dependent), actor_ref, authorization_scope, policy_version, issued_at, idempotency_key.
Returns: gap_audit_ref, gap_status=AUTHORIZED_SKIP, committed_watermark.
Errors: GAP_SKIP_UNAUTHORIZED, GAP_SKIP_AUDIT_INCOMPLETE, SOURCE_SEQUENCE_NOT_GAP, SOURCE_GENERATION_MISMATCH, IDEMPOTENCY_KEY_REUSED.
The operation is audited and append-only. It records administrative absence, not content.

### submit_late_packet(packet_ref)
Not a separate content ingestion; ingest_packet detects sequence already skipped. It appends LATE_AFTER_SKIP audit, retains packet/Evidence and schedules bounded reconciliation. Historical skip is not erased.

## 3. Proposal and ordered integration

### submit_classification(proposal)
Stores immutable proposal; no state commit. Returns proposal_ref and status PROPOSED, READY_BUT_BLOCKED, BLOCKED_BY_GAP, NEEDS_REBASE or REJECTED.

### get_packet_status(packet_ref)
Returns source_generation, source_sequence, source_range_digest, inference_status, integration_status, blocked_by_packet_refs, gap_refs, committed_watermark, rebase_required and settled_status.

### rebase_proposal(proposal_ref, expected_state_version, idempotency_key)
Creates immutable rebase record with original/current versions, read/write sets, intervening commits, validator version and outcome. No in-place base version rewrite. Returns REBASED or NEEDS_REINFERENCE.

## 4. ScopeSpan operations

### propose_scope_span(request)
Candidate only. Required scope_kind, source_ref, source_generation, source_sequence, source_digest, offset_basis, start/end, opener_anchor_ref, target candidates, packet refs, producer lineage, policy version.

### resolve_scope_continuation(request)
Core-only Delta operation. Requires current ScopeSpan ref/version, closer anchor, target refs, supporting Evidence refs, policy version and idempotency key. Validates type-specific closure. Atomically commits ScopeSpan revision and dependent Claim/Relation changes.

### invalidate_scope_span(request)
Creates append-only invalidation revision with correction source ref, invalidation basis and lineage.

Errors: SCOPE_SPAN_ANCHOR_INVALID, SCOPE_SPAN_TRANSITION_INVALID, SCOPE_CONTINUATION_UNRESOLVED, SCOPE_SCOPE_OVERREACH, SCOPE_POLICY_MISMATCH.

## 5. ContextHint and RetrievalTicket

### build_context_hint(request)
Required context_id, packet_ref/source range, max_candidate_refs, retrieval_policy_version.
Returns state_version, included_refs, omitted_ref_digest/counts, context_complete, included_ref_digest and ticket_refs. Mandatory ref ordering is normative in SPEC v0.9 §3.5.

### resolve_retrieval_ticket(request)
Required context_id, ticket_ref, expected_state_version, idempotency_key.
Returns ticket_status RESOLVED or STALE, resolved_state_version, exact refs, reasons and context_complete.
If state changed, re-evaluate stored query at current state or return STALE. Never return an old result as current. Read-only; any proposal must be separately validated.
Errors: RETRIEVAL_TICKET_STALE, RETRIEVAL_TICKET_EXPIRED, RETRIEVAL_TICKET_SCOPE_VIOLATION, CONTEXT_HINT_MANDATORY_OVERFLOW.

## 6. Benchmark profile registry and evaluation

### register_benchmark_profile(profile)
Validates JSON Schema, corpus/split digests, threshold completeness, metric version, critical labels and minimum sample counts. Stores immutable profile and digest before evaluation. Returns profile_ref/profile_digest.

### evaluate_benchmark(profile_ref, result_set_ref)
Computes coverage, abstention, per-label precision/recall/F1, selective risk, semantic agreement, critical FNR, invariant and duplicate counts, plus performance metrics.
Returns independent semantic_gate and performance_gate (PASS/FAIL/INCONCLUSIVE), overall_gate and denominator report.

### get_benchmark_result(result_ref)
Returns immutable profile/corpus/result digests, metrics, denominators, threshold comparisons and gate reasons.

Errors: BENCHMARK_PROFILE_INCOMPLETE, BENCHMARK_PROFILE_IMMUTABLE, BENCHMARK_DENOMINATOR_INVALID, QUALITY_GATE_NOT_MET, PERFORMANCE_GATE_NOT_MET.

## 7. Error code additions

SOURCE_SEQUENCE_GAP, SOURCE_SEQUENCE_REUSED, SOURCE_GENERATION_MISMATCH, PACKET_CAUSAL_PREDECESSOR_PENDING, RANGE_CONTENT_CONFLICT, METADATA_CONFLICT, GAP_SKIP_UNAUTHORIZED, GAP_SKIP_AUDIT_INCOMPLETE, SOURCE_SEQUENCE_NOT_GAP, LATE_PACKET_AFTER_SKIP, SCOPE_SPAN_ANCHOR_INVALID, SCOPE_SPAN_TRANSITION_INVALID, SCOPE_CONTINUATION_UNRESOLVED, SCOPE_SCOPE_OVERREACH, SCOPE_POLICY_MISMATCH, CONTEXT_HINT_MANDATORY_OVERFLOW, RETRIEVAL_TICKET_STALE, RETRIEVAL_TICKET_EXPIRED, RETRIEVAL_TICKET_SCOPE_VIOLATION, BENCHMARK_PROFILE_INCOMPLETE, BENCHMARK_PROFILE_IMMUTABLE, BENCHMARK_DENOMINATOR_INVALID, QUALITY_GATE_NOT_MET, PERFORMANCE_GATE_NOT_MET.

## 8. Transaction, replay and compatibility

ScopeSpan closure, Evidence links and dependent semantic Delta are atomic. Gap skip is an audited append-only administrative event. Late packet does not erase skip history. Ticket resolution is read-only. Benchmark profile is immutable. Response-loss retries return original result by idempotency key. v0.8 packets without source range digest/sequence are LEGACY_UNVERIFIED/LEGACY_UNORDERED and cannot advance v0.9 watermark.


## 9. Claim lifecycle operations

Claim lifecycle is committed through the existing `apply_semantic_delta` relation list; no in-place Claim update is exposed.

- `SUPERSEDES`: direction is new Claim to old Claim. Both endpoints must be ACTIVE Claims. The target becomes SUPERSEDED atomically.
- `RETRACTS`: direction is Evidence to Claim. The Evidence records the withdrawal basis. The target must be ACTIVE and becomes RETRACTED atomically.
- Retrieval of Claim nodes includes `claim_status`: ACTIVE, SUPERSEDED or RETRACTED.

The original Claim text and Evidence references remain immutable and retrievable. A repeated lifecycle transition against an inactive Claim is rejected. Errors: CLAIM_SUPERSESSION_ENDPOINT_INVALID, CLAIM_NOT_ACTIVE_FOR_SUPERSESSION, CLAIM_RETRACTION_ENDPOINT_INVALID, CLAIM_NOT_ACTIVE_FOR_RETRACTION. See CLAIM_LIFECYCLE_v0.1.md.


## 10. Claim temporal validity

### apply_claim_validity(request)

Required: delta_id, idempotency_key, expected_state_version, claim_id, valid_from, basis_evidence_id and contiguous revision. Optional: valid_until (empty means open-ended).

The operation appends a Claim validity revision in a SemanticDelta. Intervals are half-open `[valid_from, valid_until)`; timestamps must be RFC3339 with explicit timezone. The Core records the commit's `recorded_state_version` separately from valid time. Historical revisions remain available.

### claim_validity_at(claim_id, instant)

Returns UNKNOWN, NOT_YET_VALID, VALID or EXPIRED. Missing validity is UNKNOWN, not implicitly valid. Claim lifecycle status remains a separate dimension.

Errors: CLAIM_VALIDITY_CLAIM_INVALID, CLAIM_VALIDITY_EVIDENCE_INVALID, CLAIM_VALIDITY_TIMESTAMP_INVALID, CLAIM_VALIDITY_INTERVAL_INVALID, CLAIM_VALIDITY_REVISION_INVALID. See CLAIM_TEMPORAL_VALIDITY_v0.1.md.
