# Temporal Validity Boundary Merge v0.1

Status: Proposal-only boundary composition

## Purpose

Combine a newly recognized temporal boundary with the opposite boundary
already stored in a ClaimValidity record. This supports incremental language
such as a start date in one packet and an expiry boundary in a later packet.

## Rules

- Exactly one candidate must be supplied. Multiple candidates are ambiguous;
  this layer never ranks or selects one.
- The candidate must have status `RESOLVED` and an explicit role:
  `VALID_FROM`, `VALID_UNTIL`, or a complete `INTERVAL`.
- A `VALID_FROM` candidate replaces only the start boundary and preserves
  the stored end boundary.
- A `VALID_UNTIL` candidate replaces only the end boundary and preserves
  the stored start boundary.
- An `INTERVAL` candidate must contain both boundaries.
- All present timestamps must include a timezone. The end must be strictly
  later than the start.
- If the opposite boundary is missing, the result is
  `NEEDS_OTHER_BOUNDARY`, not a commit-ready interval.
- The result carries the expected and proposed revision numbers for
  optimistic concurrency checks. Core must revalidate the expected revision
  during commit to prevent stale merges.

## Authority boundary

Every result has `claim_validity_write=false`. This utility does not approve
a candidate, validate its source Evidence, or mutate the graph. A caller must
obtain explicit review and pass the complete proposed interval through the
existing Core-authorized ClaimValidity API. Core remains responsible for
revision monotonicity, provenance, atomic dependency evaluation, and journal
replay.

## Non-goals

This version does not infer missing years/timezones, resolve overlapping
claims, choose among multiple candidates, or perform bitemporal historical
queries.
