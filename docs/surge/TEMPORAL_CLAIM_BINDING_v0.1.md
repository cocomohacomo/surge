# Temporal Claim Binding v0.1

Status: Conservative proposal-layer association

## Purpose

Associate a recognized temporal expression with a Claim candidate only when
the source provides a narrow, auditable basis. This stage is deliberately
separate from temporal expression recognition and from Core mutation.

## Inputs

- NFC-normalized source text matching the RecognitionPacket snapshot.
- Temporal cue spans with absolute half-open UTF-8 byte offsets.
- Accepted Claim ID, if one exists.
- Evidence ID for the packet.
- Claim acceptance decision from Recognition Scoring.

## Binding gate

A cue is marked `BOUND_PENDING_CORE_REVIEW` only when all conditions hold:

1. The packet produced exactly one accepted Claim.
2. Exactly one temporal cue exists in the packet.
3. The temporal candidate status is `RESOLVED`.
4. The candidate is not marked as a correction.
5. The source contains exactly one non-empty clause under the conservative
   Japanese clause-boundary policy.
6. The source byte range decodes as UTF-8 and exactly matches the recognized
   candidate text.

All other cases receive explicit review states such as
`NO_ACCEPTED_CLAIM`, `TEMPORAL_UNRESOLVED`,
`MULTIPLE_TEMPORAL_CUES`, `MULTI_CLAUSE_SCOPE_AMBIGUOUS`,
`CORRECTION_REVIEW_REQUIRED`, or source-span integrity errors.

## Output and authority

The binding record contains Claim and Evidence references, the exact source
span, the temporal candidate, the binding basis, and an explicit
`claim_validity_write=false` marker. It is returned alongside the
ClassificationProposal and SemanticDelta result for downstream review.

This module does not create or revise ClaimValidity, infer missing dates,
select among multiple Claims, or alter prior validity history. A later Core-authorized stage may call the reviewed-commit API only after
explicit approval. It must supply a reviewer ID and a durable review Evidence
ID. The adapter rechecks the exact NFC UTF-8 source span and rejects unresolved,
corrected, non-start-boundary, or unapproved candidates. Core independently
checks Claim/Evidence types, the direct or SUPPORTS evidence link, review
Evidence, timezone-bearing interval syntax, and contiguous revision number.

The validity revision, review provenance, and any temporal Dependency
Evaluation are appended in one Core transaction. The operation is idempotent
and replay-safe. Corrections append a new revision and never overwrite earlier
history. A correction candidate is not auto-committed by this API; it requires
a separately reviewed binding.

## Conservative limitation

A single clause and a single cue are necessary but not sufficient proof of
semantic entailment. `BOUND_PENDING_CORE_REVIEW` is a high-specificity
structural association, not an automatic approval or calibrated confidence.
Human or separately validated semantic review remains required.


## Reviewed commit API

Python: `SemanticCoreBridge.apply_approved_temporal_binding(...)`.

Required inputs include the proposal binding, exact source snapshot,
`approved=True`, reviewer ID, review Evidence ID, delta/idempotency IDs,
expected state version, and the next revision number. The API is not invoked
automatically by Recognition Runtime. This keeps extraction and semantic
recognition non-committing while providing a narrow, auditable approval path.
