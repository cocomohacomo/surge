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

## Protected Question retention under capacity pressure

Question capacity demotion now protects:
- Questions explicitly associated with the currently active Focus cluster.
- Questions referenced by Open or Deferred QuestionAspects.
- Exactly one dependency hop from the complete protected anchor set. The
  neighbor set is computed from a snapshot, so relation iteration order cannot
  expand protection transitively or change which Questions remain active.
- Questions directly linked to Evidence through an About relation, preserving a
  provenance anchor.

Capacity is applied to the remaining candidates by descending interest score
with deterministic ID tie-breaking. If protected Questions alone exceed the
configured active capacity, the operation fails with
`QUESTION_MEMORY_PROTECTED_FLOOR_EXCEEDED`; it does not archive or demote
protected context to satisfy a numeric quota. This is a conservative first
retention policy. Expiry-based evidence pruning, full dependency-closure
analysis, cluster compaction and tombstone-aware deletion remain future work.

## Cluster-level retention planning increment

`ClusterRetentionPlanner` now plans capacity pressure over Cluster and
ChaosCluster records rather than treating each Question or Evidence record as
an independent memory item. Core supplies one calibrated retention priority
per candidate; that value is expected to combine importance, interest,
recency, and temporal validity without embedding language-specific weights in
the graph layer.

The planner protects:
- The active Focus cluster and its containing ancestors.
- Clusters containing Active Questions or Questions referenced by Open or
  Deferred QuestionAspects, plus their containing ancestors.
- One-hop cluster-to-cluster dependency neighbors of the protected anchor set.
- Explicitly pinned clusters and their containing ancestors.

Remaining clusters are ranked by priority descending, then last-access sequence
descending, then stable cluster ID. Lowest-priority clusters become forgetting
candidates first. If the protected floor exceeds capacity, planning fails
closed with `CLUSTER_RETENTION_PROTECTED_FLOOR_EXCEEDED`.

This increment is a pure planner: it does not mutate the graph or journal and
does not itself delete/compact data. Journaled cluster archival, shared-member
survival across multiple memberships, tombstones, replay-safe compaction, and
automatic invocation from the Core memory lifecycle remain required next steps.


## Lifecycle-aware planner and atomic application

The retention plan is bound to the ContextGraph state version from which the
complete candidate set was evaluated. A stale plan is rejected before any
mutation. ACTIVE, DORMANT, and ARCHIVED non-tombstoned clusters participate in
one importance-first ranking when selecting the bounded active set. A selected
DORMANT or ARCHIVED cluster is restored to ACTIVE; an unselected inactive
cluster remains inactive and is never emitted as a new forget action.
TOMBSTONED clusters are excluded from restoration. Inactive clusters are
reported separately and do not count as protected-floor members unless
explicitly protected. If a protected cluster is inactive, planning fails closed.

The budgeted Core gateway applies all eligible forget decisions in one
SemanticDelta. ACTIVE becomes DORMANT; a later retention pass may move DORMANT
to ARCHIVED. This is a lifecycle-only transition. It does not unlink
memberships, remove payloads, alter default retrieval filters, or compact the
graph. Archived clusters continue to protect shared member payloads during
retention planning.


## Journaled membership unlink increment

`ContextGraph::append_cluster_unmembership` stages removal of one existing
Cluster/ChaosCluster `Contains` edge in a caller-owned `SemanticDelta`.
The operation is journaled as `RELATION_REMOVE` and replayed in transaction
order. The membership edge is removed from the active graph view, but the
member node and its payload are never deleted by this operation. A shared
member therefore remains available through its other memberships; even when
the final membership is removed, payload reclamation is deliberately deferred
to a separate reference-accounted operation.

`BudgetedContextGraph::append_cluster_unmembership` routes the operation
through the existing transient/resident/event admission gateway. Lifecycle
audit membership edges cannot be removed through this API. The journal remains
append-only: removal is represented by a durable command, not by rewriting old
journal frames. Snapshot serialization sees the post-removal active relation
set, while replay applies the same removal command.

This increment does not yet implement shared-reference indexes, automatic
last-reference tombstoning, physical payload reclamation, or compaction. Those
remain separate guarded stages.


## Canonical membership reference accounting foundation

`ContextGraph::cluster_membership_reference_count` exposes a deterministic
read-only count of Cluster/ChaosCluster `Contains` edges targeting one
canonical member. Archived clusters continue to count while their membership
edge remains present. Unlinking one edge decrements the count without changing
the canonical node. A zero count is necessary but not sufficient for physical
reclamation: non-cluster relations, typed-node references, Focus, temporal
history, provenance and audit/snapshot obligations must also be checked by a
future reclamation gate. No payload deletion is performed by this counter.


## Fail-closed Entity reclamation increment

`ContextGraph::append_entity_reclamation` adds an explicit `ENTITY_RECLAIMED`
journal command. The first supported reclamation class is `Entity` only.
The gate requires the candidate to have no remaining Cluster/ChaosCluster
membership, graph relation endpoint, typed-node reference, Focus reference,
scope-span reference or dependency-evaluation history reference. A caller may
stage membership unlink and Entity reclamation in the same `SemanticDelta`;
the transaction applies unlink events before the reclamation event. Replay and
ContextGraph snapshot-tail restoration apply the same ordered commands.

`BudgetedContextGraph::append_entity_reclamation` routes the operation
through normal transient and retained-event admission. Resident reservations
remain conservative and are not released by this increment, because the
current budget facade does not maintain per-node allocation leases. The append-only
journal retains the original NODE_ADD payload until a verified snapshot checkpoint
and compaction retire that historical frame. This is logical graph reclamation,
not secure erasure or immediate journal-byte reclamation. Therefore this operation
removes the graph-owned Entity object, but does not promise an RSS reduction or
make the corresponding resident budget capacity reusable.
Evidence, Claim, Cluster, ChaosCluster, lifecycle records and other node kinds
are deliberately ineligible until their external provenance, temporal and
audit references can be proven clear. Physical compaction and exact lease
reconciliation remain future work.


## Entity reclamation checkpoint/compaction conformance increment

The Entity reclamation path now has an end-to-end conformance test against the
existing SnapshotCoordinator::compact workflow. The test:

- commits an Entity and its Cluster membership, then atomically unlinks and
  reclaims that Entity;
- captures the post-reclamation graph at the journal's exact state watermark;
- durably saves the snapshot, verifies the primary generation, mirrors the
  verified generation to the backup, and only then compacts the journal;
- verifies that the compacted journal has no active historical event frames
  and that the snapshot no longer contains the reclaimed Entity payload;
- opens a fresh journal/graph instance and restores from the checkpoint,
  confirming that the Entity remains absent while the containing Cluster
  remains present;
- forces snapshot persistence failure and confirms that the journal is not
  marked compacted and its replayable events remain available.

This proves logical graph reclamation plus checkpoint-based retirement of the
old journal frames for the tested Entity path. It does not prove secure erasure
from filesystem media, allocator page release, RSS reduction, or bounded
snapshot capture memory. The generic coordinator's staging limits and logical
budget remain the current controls; target edge-device measurements are still
required.


## Entity resident reservation release and verified journal-backup pruning

The budgeted Entity reclamation path now releases the corresponding logical
ResidentBytes reservation after the graph transaction commits. Before commit,
the gateway verifies that the candidate is an Entity and that active resident
leases cover the exact node reservation; if the accounting lease is missing,
the operation fails closed. The lease supports partial release so a shared
aggregate recovery or mutation reservation can be reduced without releasing
unrelated nodes. The graph reclamation gate still independently proves that
the Entity has no remaining graph or cluster references.

The existing explicit journal-backup pruning operation is now included in the
Entity checkpoint integration test. It requires the active compacted journal,
the primary snapshot, and the separately readable snapshot backup to agree on
the checkpoint watermark and context. It verifies the retained pre-compaction
journal reaches the expected watermark before removing that journal backup,
then syncs the parent directory. The snapshot backup remains available.

This is logical resource accounting, not allocator page release or an RSS
reduction guarantee. A crash after unlinking the backup but before directory
sync may leave platform-dependent recovery state; secure media erasure is not
claimed. SnapshotCoordinator now enforces a configurable logical capture-staging ceiling across each domain payload, aggregate bundle and final envelope duplication. Each domain callback is separately bounded by max_domain_payload_bytes. The callback's own allocation occurs before its returned size can be checked, so this is not a hard allocator/RSS ceiling; a streaming/segmented domain capture API remains necessary for strict peak-memory control.


## Chunked snapshot domain capture API increment

SnapshotDomain now supports a chunk-writer capture contract. SnapshotCoordinator
streams each domain's chunks directly into its aggregate framed bundle while
enforcing cumulative domain and aggregate limits. Domains that override this
method avoid a second full-sized per-domain payload string in the coordinator.
The compatibility implementation still adapts the legacy string capture
method, so existing domains retain behavior but do not yet gain the full
memory benefit. Built-in domain serializers and SnapshotStore direct streaming
remain follow-up work; current aggregate bundle/envelope materialization means
this is not a hard RSS bound.


## Importance-driven forgetting and restoration priority

- `ClusterRetentionCandidate::knowledge_cluster` marks knowledge-bearing clusters. Knowledge clusters and their ancestor chains are protected from capacity forgetting, independent of importance or access recency.
- For non-knowledge clusters, inactive clusters (DORMANT/ARCHIVED) are considered for restoration before active clusters compete for remaining retention capacity. Within the same lifecycle tier, retention priority descends, then last-access sequence descends, then cluster ID provides stable tie-breaking.
- Selected inactive clusters are emitted as `restore_cluster_ids` and restored to ACTIVE by the budgeted lifecycle gateway. This restoration is staged atomically with forget transitions in the same journal transaction.
- Low-priority active clusters are transitioned to DORMANT first; subsequent eligible passes can archive them. This is lifecycle compression, not payload deletion or physical memory reclamation. The current graph remains canonical and full restore is available while its payload and journal/snapshot obligations remain intact.
- Pinned inactive clusters remain fail-closed. Knowledge clusters can be restored from DORMANT/ARCHIVED states, but tombstoned clusters are not automatically restored.
- Focus, active/open questions, dependencies, pinned clusters and knowledge clusters remain protected. Capacity cannot override the protected floor.


### Ranking and dormant restoration clarification

Retention priority is the primary ranking key across active and inactive non-knowledge clusters. An inactive cluster wins a tie at equal priority, so restoration is preferred when it does not displace a more important active cluster. Last-access sequence and stable cluster ID break subsequent ties. Both DORMANT and ARCHIVED selected clusters are transitioned back to ACTIVE in the same atomic lifecycle delta. Tombstoned clusters remain excluded from automatic restoration.

The lifecycle planner does not claim physical memory reclamation. DORMANT/ARCHIVED currently changes retention state while canonical graph payload remains available for deterministic recovery. Physical payload offload/compression and measured RSS reduction require a separate storage-backed implementation and benchmark.


### Importance-first reversible restoration

Restoration competes for the same configured active-cluster slots as retention.
This prevents a low-importance active cluster from occupying capacity while a
higher-importance archived cluster remains unavailable. Importance is the
primary key; lifecycle state is only a deterministic tie-breaker. Inactive
clusters that lose the ranking remain inactive without generating redundant
forget transitions. Shared members are preserved whenever any retained or
still-inactive cluster continues to reference them. This is still a logical
lifecycle policy: it does not by itself offload payload bytes or reduce RSS.


## 8. Durable cluster payload archive foundation

`ClusterArchiveStore` is the first storage primitive for reversible payload
offload. Each cluster uses its own archive path. The record binds the payload
to the cluster ID, graph state version, and journal sequence, and is encoded
with the existing snapshot envelope. Atomic generation replacement, SHA-256
integrity validation, and last-known-good backup recovery are inherited from
`SnapshotStore`.

The archive API rejects empty identities/payloads and zero state versions.
Restore validates the expected cluster identity and a caller-supplied minimum
state version before returning payload bytes. A corrupt primary may recover
from the authenticated backup generation.

This stage intentionally does not detach or erase `ContextGraph` nodes,
change lifecycle state, or claim RSS reduction. The caller must first produce a
complete cluster payload snapshot and prove that every referenced member is
safe to offload. Graph detachment is deferred until the graph has an explicit
archive-handle/reference-count representation and a journaled atomic commit
protocol. Until then, the archive is a durable copy, not a memory-reclamation
operation.


## Archive restore freshness guard

`ClusterArchiveStore::restore` now accepts a minimum journal sequence in addition to the minimum cluster state version. The archive is rejected unless both version floors are met, preventing a valid but stale archive generation from being used as a restoration source. The default sequence floor is zero for API compatibility. This is a preflight freshness guard only: callers must still validate graph state and commit restoration transactionally before mutating `ContextGraph`.


## Shared-member-safe archive detach planning

A conservative, read-only `plan_cluster_archive_detach` API now evaluates proposed
cluster members before any future physical reclamation. It only marks an Entity
as exclusively owned when it has exactly one active Cluster/ChaosCluster
membership, that membership belongs to the target cluster, and no other graph
relation or typed node reference points to the Entity. Shared membership,
non-Entity nodes, missing nodes, and nodes with additional references are
classified separately as ineligible or shared.

The planner is deliberately non-mutating. It does not detach graph nodes,
remove journal relations, or claim resident-memory reduction. The archive must
be durably written and verified before a caller can use a later transactional
detach operation. The existing graph reclamation command remains the final
guard and must reject any references that survive unmembership.
