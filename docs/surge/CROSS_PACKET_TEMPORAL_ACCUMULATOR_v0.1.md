# Cross-Packet Temporal Accumulator v0.1

Status: In-memory proposal accumulator; no Core mutation.

## Purpose

Retain complementary temporal boundaries recognized in separate packets,
provided the caller explicitly associates every contribution with the same
Claim ID. For example, a packet may establish `VALID_FROM` and a later packet
may establish `VALID_UNTIL`.

## Contract

- Claim identity is supplied by the caller. This module does not infer that
  two packets concern the same Claim.
- Every contribution requires an Evidence ID and packet ID; both are returned
  as provenance for review.
- Only one resolved candidate per call is accepted.
- A missing opposite boundary yields `NEEDS_OTHER_BOUNDARY`.
- Conflicting values for an already supplied boundary yield
  `BOUNDARY_CONFLICT_REVIEW_REQUIRED`; no latest-wins policy exists.
- The accumulator is bounded by `max_claims` (default 4096). At capacity it
  rejects new Claim keys rather than silently evicting context.
- Completed proposals remain pending until the caller explicitly clears the
  Claim key after disposition.

## Authority and durability

This accumulator is process-local and in-memory. It is not a journal, does not
survive restart, does not retrieve the latest ClaimValidity revision, and does
not prove that Evidence supports the Claim. Its output always has
`claim_validity_write=false` and requires the existing reviewed Core API.
The caller must re-read the current Core revision and revalidate the interval
and provenance at commit time. A stale proposal must be rebuilt.

## Next integration boundary

A future durable implementation should persist pending boundary contributions
as typed journal events, link each contribution to its Evidence and Claim,
and resolve them under a state-version/revision precondition in the same Core
transaction. Do not treat this in-memory helper as cross-process state.
