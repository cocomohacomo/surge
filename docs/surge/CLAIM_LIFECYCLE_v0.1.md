# Claim Lifecycle and Append-Only Correction v0.1

Status: implementation contract for the Core Claim graph

## 1. Purpose

A Claim is an evidence-backed assertion. A later correction must not overwrite its text, Evidence references, or provenance. SURGE retains each Claim and records an explicit lifecycle relation so retrieval can distinguish current assertions from historical assertions.

## 2. Lifecycle states

- `ACTIVE`: the Claim is currently asserted by the graph and has not been superseded or retracted.
- `SUPERSEDED`: a newer Claim replaces this Claim. The old Claim and its Evidence remain addressable.
- `RETRACTED`: the Claim has been explicitly withdrawn. Its original content and Evidence remain addressable.

A Claim is `ACTIVE` when first committed. Lifecycle state is derived from committed relations and reconstructed during Journal replay. No mutable update operation exists for Claim content or Evidence references.

## 3. Relation contracts

### 3.1 SUPERSEDES

Direction: `new_claim --SUPERSEDES--> old_claim`.

Requirements:
- Both endpoints MUST be Claim nodes.
- Both Claims MUST be ACTIVE at the time the relation is committed.
- The target Claim transitions to SUPERSEDED in the same SemanticDelta transaction.
- The source Claim remains ACTIVE.
- A second supersession of the same old Claim is rejected.
- The correction Claim MUST carry its own Evidence references. Prior evidence is not silently inherited.

### 3.2 RETRACTS

Direction: `evidence --RETRACTS--> claim`.

Requirements:
- The source MUST be an Evidence node that records the basis for withdrawal.
- The target MUST be an ACTIVE Claim.
- The target transitions to RETRACTED in the same SemanticDelta transaction.
- Retraction does not delete the Claim or its original Evidence.
- A second retraction, or retraction of a superseded/retracted Claim, is rejected.

The relation direction is intentionally explicit to avoid ambiguity about which assertion is new and which is historical.

## 4. Atomicity and replay

Claim creation, lifecycle relation insertion and status transition are staged in the same ContextGraph SemanticDelta. Validation failure rejects the whole Delta. The relation is journaled as an ordinary `RELATION_ADD` event; replay applies the same endpoint and state validation and deterministically reconstructs lifecycle status.

Idempotent retries return the original transaction result. A conflicting idempotency-key reuse remains rejected. Historical Claims and relations are never rewritten.

## 5. Retrieval contract

Claim retrieval records include `claim_status` with one of `ACTIVE`, `SUPERSEDED`, or `RETRACTED`. Retrieval MUST NOT silently omit historical Claims merely because they are no longer active. Consumers that need a current-state view should filter explicitly to `ACTIVE`; audit and explanation views should retain all states.

## 6. Error behavior

- `CLAIM_SUPERSESSION_ENDPOINT_INVALID`: SUPERSEDES endpoints are not both Claims.
- `CLAIM_NOT_ACTIVE_FOR_SUPERSESSION`: either Claim is not ACTIVE.
- `CLAIM_RETRACTION_ENDPOINT_INVALID`: RETRACTS is not Evidence-to-Claim.
- `CLAIM_NOT_ACTIVE_FOR_RETRACTION`: target Claim is not ACTIVE.

All errors fail closed and leave state version and Journal unchanged.

## 7. Conformance cases

- Create an Evidence-backed Claim and verify ACTIVE.
- Add a newer Evidence-backed Claim and SUPERSEDES relation; verify old=SUPERSEDED and new=ACTIVE.
- Attempt a second supersession of the same old Claim; reject the full Delta.
- Add Evidence and RETRACTS relation; verify target=RETRACTED.
- Restart and verify all statuses and relations are reconstructed.
- Verify the historical Claim remains retrievable with its original content and Evidence references.

## 8. Scope boundary

This version implements explicit correction and retraction relations, durable lifecycle state and retrieval visibility. It does not yet implement temporal valid-time intervals, automatic propagation of status to derived Claims, or policy-driven conflict resolution. Those remain separate follow-up work.
