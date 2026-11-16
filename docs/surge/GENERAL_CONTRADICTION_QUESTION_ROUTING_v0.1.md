# General Claim Contradiction Question Routing v0.1

## Purpose

Expose the existing QuestionRouter as a durable Core API for ordinary
Claim-to-Claim contradiction relations. This complements the temporal
Question lifecycle without creating a second Question model.

## API

The C ABI function surge_question_route_contradiction and Python method
SemanticCoreBridge.route_contradiction(relation_id, policy_version=...)
accept an existing CONTRADICTS relation between two Claim nodes.

The route is deterministic by relation ID:

- Question ID: question/contradiction/{relation_id}
- QuestionAspect ID: aspect/contradiction/{relation_id}
- one Interest is created for each Cluster reached by an ABOUT relation from either endpoint Claim
- Focus is selected only when exactly one distinct Cluster is linked
- zero or multiple linked Clusters do not change existing Focus
- repeated calls return the existing route and do not advance journal state

## Lifecycle and authority

This API creates the durable Question and QuestionAspect in the shared
EventJournal. Resolution remains an explicit QuestionAspect status operation.
Creating or routing a Question does not resolve the contradiction, mutate
ClaimValidity or ClaimStatus, rank Evidence, or execute an external action.

The route API is explicit: a producer calls it after its contradiction
recognition has been accepted and the corresponding CONTRADICTS relation
has been committed. This keeps probabilistic recognition outside the
authoritative Core graph mutation.

## Verification

The conformance suite covers a single-cluster route, ambiguous multi-cluster
routing, duplicate idempotency, and restart/replay persistence. Existing C++
QuestionRouter conformance tests continue to cover invalid relation handling.

## Scoped re-evaluation API v0.2

The additive `route_contradiction_scoped(relation_id, scope_key, policy_version=...)` API groups accepted contradiction events by the unordered pair of Claim IDs and the exact caller-supplied scope key. Scope is mandatory and is never inferred by the Core.

- Question identity is stable for the normalized Claim pair plus scope key.
- Each accepted contradiction relation receives a separate QuestionAspect under that Question. A new event therefore reopens the aggregate Question by adding a new OPEN aspect; resolved historical aspects remain intact.
- Aspect and Interest content retain the source relation ID and scope key. Original Evidence and Packet provenance remains on its source graph records.
- Reversing relation endpoint order yields the same Question identity; a different scope key yields a different Question.
- Replaying the same relation/scope is idempotent.

The legacy relation-ID API is unchanged. This API does not infer scope equivalence, mutate ClaimValidity or ClaimStatus, resolve an aspect, rank evidence, or trigger external actions.
