# SURGE Clustered Input Retention v0.1

## Normative principle

SURGE ingests information into a persistent, queryable cluster graph. The
ingress path must not treat accepted input as a transient scoring buffer whose
content disappears after a decision. Every admitted interpretation, evidence
item, entity, scope, and unresolved hypothesis must have an explicit cluster
context and provenance path.

Retention remains bounded. Forgetting, archival, or compaction is a deliberate
Core retention decision and must preserve the configured audit/tombstone
contract; it is not silent loss in an adapter.

## Cluster hierarchy

- A context/root Cluster anchors the stream or session.
- Core classification creates typed child Clusters beneath that root.
- A typed cluster is deterministically identified by its parent, classification
  label, and partition key.
- Claims, Entities, Evidence and nested Clusters can have explicit membership
  through `RelationKind::Contains`.
- A member may belong to multiple clusters. This supports cross-topic,
  cross-span, temporal, entity, and discourse relationships without copying
  the underlying record.
- Cluster-to-cluster, cluster-to-claim, cluster-to-entity and
  cluster-to-evidence containment are valid graph relations. Question-to-
  Question containment remains supported for question decomposition.

## Core API

`ContextGraph::append_cluster_membership` appends a typed membership to a
`SemanticDelta`. It validates the parent, derives a stable child-cluster ID,
and adds the parent/child and child/member containment edges to the same delta.
The caller supplies classification and partition hints; SURGE owns cluster
identity, graph mutation, validation, atomic commit and replay.

Adapters must not maintain a parallel cluster store or write ad-hoc membership
relations. They may supply language-specific hints, but all accepted records
must enter the Core graph and journal.

## kana-decoder integration profile

The Japanese normalization stream currently uses these typed classifications:

- `LEXICAL_INTERPRETATION`: candidate Claim records for a reading span.
- `SURFACE_REALIZATION`: original and Language Set-rendered surface Entities.
- Claim-linked Evidence retains lexical, morphology, transition, n-gram, POS and
  context-cluster observations, including source digest and temporal validity.
- Contradictory Claims are connected and routed through Question handling.
- Unresolved candidate sets are retained in Chaos Cluster.

These are language-runtime classifications, not special-purpose decision
engines. Other Language Sets may define additional typed classifications
without changing the Core membership contract.

## Required invariants

1. A committed input member has at least one parented Cluster membership.
2. Parent Cluster, child Cluster, member node and membership edges are committed
   in one SemanticDelta.
3. Replaying the journal reconstructs identical cluster IDs and memberships.
4. Repeated classification in the same transaction is idempotent by stable IDs.
5. Cluster membership never silently changes a Claim's semantic identity.
6. Cross-cluster membership references the same underlying node.
7. Resource limits and retention policy are enforced by Core.
8. Any forgotten/archived content follows explicit retention/tombstone policy.

## Conformance

Tests must cover deterministic IDs, invalid parents, allowed member kinds,
multiple memberships, duplicate/replayed transactions, and recovery after
restart. Runtime tests must prove that interpretation and surface records are
clustered without an adapter-owned shadow store.
