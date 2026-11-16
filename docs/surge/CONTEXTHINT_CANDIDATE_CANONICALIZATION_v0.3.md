# ContextHint Candidate Canonicalization v0.3

## Problem found

Graph traversal can discover the same reference through multiple paths. The Python adapter previously retained only the first candidate for a reference. That made output dependent on discovery order: a later, stronger class could be discarded, and better source-overlap evidence could be lost. The C++ hint builder already sorts candidates before deduplication, so this also created a Python/Core behavioral mismatch.

## Rule

For each reference, canonicalization now applies:

1. Keep the candidate class with the strongest fixed ContextHint priority.
2. Keep the maximum non-negative integer source_overlap.
3. Keep the minimum non-negative integer dependency_depth.
4. Sort canonical candidates by class priority, descending overlap, ascending dependency depth, then reference ID.

Metadata rejects booleans, non-integers and negative values. This is intentional because Python treats bool as a subclass of int; accepting True as a score would silently create a malformed ranking input.

## Invariants

- Candidate output is independent of discovery order.
- A weaker duplicate cannot downgrade a stronger classification.
- A stronger duplicate cannot be hidden by an earlier weak candidate.
- Evidence score and graph distance are aggregated independently from class.
- One canonical candidate exists per reference before budget calculation.
- Mandatory overflow and RetrievalTicket content are computed from canonical candidates, not raw duplicate count.

## Regression coverage

The regression tests cover class promotion after an earlier low-priority candidate, aggregation of maximum overlap and minimum depth, deterministic budget ordering, ticket omission, and invalid metadata rejection.

## Scope

This patch does not change graph traversal, dependency semantics, Focus, QuestionAspect lifecycle, or Core persistence. It closes the deterministic candidate canonicalization gap before expanding dependency condition semantics.
