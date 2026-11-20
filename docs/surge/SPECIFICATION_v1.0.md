# SURGE 基礎設計 v1.0
## Universal Context Cognition Framework
### Open-World Recognition and Cluster-Native Memory

Version: 1.0.1 (normative clarification)
Parent: SPECIFICATION_v1.0.md
Detailed Chaos Cluster model: CHAOS_CLUSTER_MODEL_v1.0.md
Cluster retention contract: CLUSTERED_INPUT_RETENTION_v0.1.md
Schema: schemas/surge-chaos-cluster-v1.0.schema.json
Conformance: tests/conformance/SURGE_CHAOS_CLUSTER_v1.0.json

## 1. Version scope and status

This document clarifies the v1.0 normative memory model. It retains all v1.0
open-world recognition, Chaos Cluster, source-integrity, immutable Evidence,
Claim lifecycle, temporal validity, Question/Interest/Focus, transaction,
replay and quality-gate contracts.

This clarification is additive. It does not claim that all cluster lifecycle,
forgetting, compaction or runtime integrations are implemented. Normative
specification, executable conformance and measured performance are separate
claims.

## 2. Fundamental memory invariant: all information is cluster-resident

SURGE is a cluster-native cognition framework. The canonical memory is a
persistent, typed, many-to-many Cluster Graph. Information is not stored in
independent parallel memory silos and later optionally grouped. Every admitted
information item MUST be represented as a graph node or immutable source/event
reference and MUST have at least one explicit Cluster or ChaosCluster
membership in the same atomic admission transaction.

This rule applies regardless of input granularity or type, including a single
acknowledgement, a short utterance, a document span, an Evidence item, a Claim,
an Entity, a Question, an Interest observation, a recognition candidate or a
temporal observation. Small inputs are not presumed unimportant. Importance
and meaning are contextual and may be revised as later events arrive.

A node may belong to multiple clusters. Membership edges reference the same
canonical node; implementations MUST NOT duplicate the underlying payload to
simulate multi-membership. Cluster membership means retention and contextual
association, not semantic equivalence, truth, causality or agreement.

## 3. Cluster graph as the canonical memory substrate

- Cluster and ChaosCluster are first-class memory containers and either
  kind may parent a containment membership.
- Either cluster kind may contain nested Cluster/ChaosCluster nodes and typed
  information nodes, including Entity, Evidence, Claim, Question,
  QuestionAspect, Dependency and Interest. Question-to-Question containment
  remains available for question decomposition.
- Typed nodes retain their own semantic type and provenance while being
  addressable through one or more cluster memberships.
- Cluster-to-cluster containment establishes hierarchy; cross-cluster
  membership and typed relations express overlapping contexts.
- ChaosCluster preserves unresolved, plural, weakly-associated and exploratory
  information. It is not a garbage bin or a low-value tier.
- Question, Interest and Focus influence attention and retrieval over the
  graph. They are not separate stores that can bypass cluster membership.
- Journals, snapshots, indexes and caches are projections/recovery mechanisms;
  they do not replace the canonical cluster graph.

Every accepted event is assigned to an existing or newly classified cluster.
If classification is uncertain, it MUST be retained in a suitable
ChaosCluster or explicitly unresolved cluster. It MUST NOT be silently dropped
or kept only in a transient scoring buffer.

## 4. Recognition, classification and membership transaction

Admission MUST atomically persist:
1. source/event identity and provenance;
2. the typed information node or immutable source reference;
3. at least one parented cluster membership;
4. any classification, temporal and decision trace needed to reproduce the
   admission.

The parent cluster, child cluster, member and membership edges are committed
in one SemanticDelta or equivalent Core-owned transaction. Retry is
idempotent. Replay reconstructs identical cluster IDs and memberships.

Adapters may provide language-specific classification hints, but they MUST NOT
maintain a parallel context/memory graph or bypass the Core membership gateway.
A failed admission leaves graph version, journal watermark, membership and
idempotency state unchanged. A resource-budget rejection applies backpressure
or returns a retryable failure; it never silently loses input.

## 5. Importance and attention are contextual observations

Importance is not identical to recency, frequency, confidence, coherence or
cluster size. Retention policy may consider:
- current and historical Focus relevance;
- explicit and inferred Interest with provenance;
- unresolved Questions and contradiction pressure;
- Claim/Evidence support, conflict and temporal validity;
- dependency and shared-reference structure;
- recurrence, novelty, surprise and diversity;
- last access and elapsed time;
- source reliability, provenance value and audit obligations;
- storage cost and configured resource ceilings.

These dimensions MUST remain inspectable and provenance-bound. Missing values
are UNKNOWN, not zero. A single scalar may be used for bounded ranking only
after policy evaluation; it MUST NOT independently authorize truth assignment
or eviction of protected context. Policy version and contributing factors
must be available for audit and deterministic replay.

## 6. Forgetting is cluster-prioritized, reference-safe and reversible where required

Forgetting is a normal memory lifecycle operation. Under bounded capacity,
SURGE prioritizes the lowest-retention-value eligible clusters first. It does
not independently evict arbitrary nodes or discard individual events merely
because they are old, incoherent or temporarily out of focus.

The lifecycle distinguishes:
- ACTIVE: available to current focus and default retrieval.
- DORMANT: deprioritized but directly reactivatable.
- ARCHIVED: excluded from default retrieval, recoverable by explicit action
  while retained payload policy permits.
- TOMBSTONED: payload or membership has been removed under an explicit,
  journaled policy; the tombstone preserves permitted identity, reason,
  provenance digest and lineage metadata.

The Core graph now supports append-only `ClusterLifecycle` records attached to
 their target Cluster/ChaosCluster. Each record carries a monotonic per-cluster
revision, RFC3339 observation text, policy reason and a 64-character lowercase
SHA-256 provenance digest. Allowed transitions are ACTIVE to DORMANT or ARCHIVED; DORMANT to ACTIVE
or ARCHIVED; and ARCHIVED to ACTIVE or TOMBSTONED. TOMBSTONED is terminal,
so payload retirement requires an explicit prior ARCHIVED state. Lifecycle records
are ordinary journaled graph nodes, so replay and graph snapshots retain the
history. A TOMBSTONED record is currently a logical tombstone marker only: it
does not remove membership edges or reclaim the target payload. Physical
membership removal and payload reclamation remain separate, unimplemented
operations and MUST NOT be inferred from this marker.

A cluster may be archived only after evaluating its protection floor,
dependencies, active questions, Focus, valid evidence, audit constraints and
shared members. A member referenced by any retained cluster MUST survive
forgetting of another cluster. A shared node is eligible for payload deletion
only when no retained membership, protected dependency, journal/snapshot
recovery obligation or legal retention requirement remains.

Removing a cluster membership MUST NOT delete the member node. Deleting a
payload MUST be a separate explicit operation with a tombstone and policy
reason. Parent/child lineage and cross-cluster relations must be updated or
preserved according to the tombstone contract. No silent cascade deletion is
permitted.

The retention planner MUST evaluate a complete candidate set. Omitted clusters
cannot become immortal by escaping the planner, and malformed/non-finite
priorities fail closed. Active Focus, unresolved Questions, valid protected
Evidence dependencies, explicitly pinned clusters and configured audit roots
form a protected floor. If that floor exceeds capacity, SURGE rejects or
defers reclamation rather than deleting protected memory.

## 7. Temporal validity and reassessment

Evidence and Claims retain valid-time and observation-time history. Expiry
changes current applicability, not historical existence. Retention priority
may fall when evidence expires, a claim is retracted or dependencies resolve,
but history and provenance remain queryable according to policy.

Reassessment is append-only and version-pinned. A later event may revive an
older cluster, alter its importance, create a new relation or change Focus
without rewriting prior recognition decisions. Forgetting a cluster does not
assert that its contents were false.

## 8. Bounded streaming memory and resource safety

The memory model is streaming-first and edge-constrained. Every allocation-
heavy admission, classification, decision, mutation, snapshot restore and
retention operation MUST reserve bounded transient and resident resources
before materializing unbounded data.

Resource admission failure MUST be atomic and observable. The system MUST
preserve input ordering, apply backpressure or explicit retry semantics, and
avoid silent input loss. Protected memory cannot be reclaimed merely to satisfy
a configured numeric cap. If the protected floor exceeds capacity, fail
closed and report the required capacity.

Logical byte reservations, allocator usage and process RSS are distinct
measurements. A logical ResourceBudget is not a hard RSS limiter. Performance
claims require target-device peak RSS, transient high-water, queue depth and
p50/p95/p99 latency measurements under sustained streaming load.

## 9. Chaos Cluster relationship

ChaosCluster is a first-class cluster type for unresolved, plural, exploratory
or weakly organized information. Its members remain fully eligible for future
retrieval and reclassification. Low coherence MUST NOT be treated as low
importance. A ChaosCluster may contain valuable novel observations and must
participate in the same explicit retention policy, with diversity and novelty
considered as independent factors.

## 10. Conformance requirements

The following are separate test obligations:
- admission coverage: every public input route creates/uses cluster membership;
- multi-membership: shared nodes survive archival of one containing cluster;
- protected floor: Focus, unresolved Question, valid Evidence and pinned roots;
- retention ranking: deterministic priority, recency and stable-ID tie-breaks;
- temporal reassessment: expiry changes ranking without erasing history;
- tombstone lifecycle: journal replay, snapshot restore and idempotent retries;
- compaction: bounded staging, atomic commit and crash-safe recovery;
- streaming: backpressure and no silent loss under memory pressure;
- edge performance: target hardware peak RSS, transient high-water, queue depth,
  p50/p95/p99 latency and sustained throughput.

A planner-only test does not prove actual forgetting. A journal unit test does
not prove end-to-end input admission. CI does not substitute for target-device
measurement.

## 11. Implementation status

The repository currently contains a typed ContextGraph, deterministic cluster
membership helper, budgeted C ABI mutation gateway, Question/Focus retention
guards, a pure ClusterRetentionPlanner and snapshot restore reservations.
These are implementation increments.

The following remain incomplete and MUST NOT be represented as finished:
- all public C++, Python, ONNX and adapter ingress paths mandatorily routed
  through the cluster membership gateway;
- durable cluster archive/tombstone transactions and membership removal;
- shared-member reference accounting and safe payload reclamation;
- expiry-aware cluster retention scoring and automatic lifecycle invocation;
- bounded cluster compaction with crash/replay and snapshot integration;
- shared finite budget across all native, morphology, n-gram, ONNX and
  serialization allocations;
- actual target edge-PC RSS/RTF and latency measurements.

## 12. Compatibility

This is a normative clarification to v1.0 and does not silently reinterpret
legacy records. Existing unclustered records require an explicit migration
transaction that creates a cluster membership with source provenance. Until
migration succeeds, they remain legacy-unclassified and MUST NOT be treated as
fully conformant cluster-native memory.


### ClusterLifecycle construction API

The Core exposes `ContextGraph::append_cluster_lifecycle` to encode a typed `ClusterLifecycleRecord` into the journal payload and append its required `Contains` edge to a caller-owned `SemanticDelta`. The helper validates target identity, revision continuity, timestamp, digest syntax, reason bounds and allowed state transitions before changing the delta. The graph itself remains unchanged until the caller commits the delta through the normal (preferably budgeted) mutation gateway. This helper does not remove memberships, filter retrieval, reclaim payloads, or provide a hard process-memory bound.


### Lifecycle-aware retention execution increment

The retention planner records the graph state version from which its complete
candidate set was evaluated. ARCHIVED and TOMBSTONED clusters are reported as
already inactive and do not consume active retention capacity. DORMANT clusters
remain recoverable but rank behind ACTIVE clusters, independent of raw
priority. If an inactive cluster is part of the protected floor, planning fails
closed. The budgeted mutation gateway can apply all forget decisions in one
SemanticDelta: ACTIVE targets move to DORMANT, and DORMANT targets move to
ARCHIVED. A stale plan is rejected before mutation. This operation changes
lifecycle metadata only; it does not remove memberships, filter retrieval,
delete payloads or compact the graph.


### Journaled cluster membership unlink implementation increment

`ContextGraph::append_cluster_unmembership` and its budgeted gateway
counterpart now stage an explicit removal of an existing Cluster/ChaosCluster
`Contains` edge. The journal records `RELATION_REMOVE` so replay
reconstructs the same active membership graph. The member node is preserved
regardless of whether other memberships remain. ClusterLifecycle audit edges
are protected from this operation. This is an unlink-only operation, not
payload deletion: shared-reference accounting, final-reference reclamation,
tombstone coupling and compaction remain incomplete.


### Canonical membership reference accounting foundation

The Core exposes `cluster_membership_reference_count` as a read-only
count of Cluster/ChaosCluster membership edges for a canonical member. Archived
clusters remain references until their membership edge is explicitly unlinked.
A zero membership count is not, by itself, authorization to reclaim a payload:
all typed references, dependencies, provenance, temporal history, Focus,
journal/snapshot and audit obligations must be evaluated by a separate
reclamation gate. This increment does not delete payloads.


### Fail-closed Entity reclamation implementation increment

The Core now supports `ENTITY_RECLAIMED` as an append-only journal command
for `Entity` nodes only. Reclamation is rejected while any cluster
membership, graph relation, typed-node reference, Focus, scope-span or
dependency-evaluation history reference remains. Membership unlink and
reclamation can be staged in one `SemanticDelta`, with unlink events
applied first. Journal replay and ContextGraph snapshot-tail restore apply the
same ordered transition.

This is not a general payload garbage collector. Evidence, Claim, cluster and
lifecycle nodes remain protected because their provenance, temporal and audit
obligations require broader reference accounting. The budgeted gateway charges
the retained reclamation event but conservatively retains prior resident
reservations; it does not claim RSS reduction or reusable resident capacity. The
append-only journal retains the original node-add payload until a verified snapshot
checkpoint and compaction retire that historical frame. This is logical graph
reclamation, not secure erasure or immediate journal-byte reclamation.
Exact per-node lease release and cross-domain reference accounting remain incomplete. Verified checkpoint-based compaction is exercised by a dedicated Entity reclamation conformance test. The journal backup generation intentionally retains pre-compaction frames for rollback until a separate backup-retention policy retires them.


### Entity reclamation checkpoint and compaction conformance increment

A dedicated end-to-end test now exercises the existing
SnapshotCoordinator::compact path after an Entity has been unlinked and
reclaimed. The test verifies snapshot watermark and context identity, durable
primary/backup verification before journal replacement, removal of historical
journal events from the active generation while retaining the pre-compaction
journal backup for rollback, and fresh-instance restoration in
which the reclaimed Entity stays absent. A forced snapshot-store size failure
must leave the journal uncompacted and its events replayable.

This is logical reclamation and journal-history retirement, not secure media
erasure or an RSS guarantee. Snapshot capture still constructs bounded
serialized payloads, and the current logical staging budget is not a hard
allocator/RSS ceiling. Edge-PC peak-memory and latency measurements remain
outstanding.


### Entity ResidentBytes lease reconciliation and journal-backup pruning

BudgetedContextGraph now releases the exact logical Entity node reservation
after a successful, reference-checked ENTITY_RECLAIMED transaction. It first
preflights that the active ResidentBytes leases cover the candidate reservation,
then performs partial lease releases only after the journal-backed graph
mutation commits. A missing lease causes fail-closed rejection before commit.
This preserves unrelated reservations in shared aggregate leases. It does not
release allocator pages or guarantee lower process RSS.

The Entity checkpoint conformance test also exercises the existing explicit
journal-backup pruning API. Pruning requires a verified compacted active journal,
readable primary and backup snapshot generations at the same checkpoint, and a
valid retained journal backup at the expected watermark. It then removes the
journal backup and syncs the parent directory while retaining the snapshot
backup. Snapshot capture remains string-materialized within configured payload
and envelope limits; a streaming/segmented capture API is still needed to
provide a stricter peak-memory bound.


### Snapshot capture staging guard

SnapshotCoordinatorLimits now includes max_capture_staging_bytes. Capture checks
the combined known staging footprint of the current aggregate bundle, returned
domain payload, field framing, and the final envelope duplication before
continuing. Each domain payload also remains bounded by
max_domain_payload_bytes. The conformance test rejects a capture whose
calculated staging peak exceeds the configured ceiling.

This is a logical bound over known serialized buffers, not a hard allocator or
RSS ceiling: a domain's capture callback may allocate its bounded payload
before the coordinator can inspect its returned size. Strict peak-memory
control still requires a streaming/segmented SnapshotDomain capture contract.


### Chunked SnapshotDomain capture contract

SnapshotDomain now exposes capture_chunks with a bounded chunk writer. The
coordinator frames each domain's payload length before streaming chunks directly
into the aggregate bundle, validates cumulative per-domain and aggregate limits
for every chunk, and patches the final length only after successful completion.
This removes the coordinator-owned full per-domain payload copy for domains
that implement the streaming override.

The compatibility default adapts the legacy capture(string*) method and
therefore still materializes one full domain payload. Built-in snapshot domains
have not yet migrated their serializers to the streaming override, and the
coordinator still materializes the aggregate bundle and final envelope.
Consequently this is an API and coordinator staging increment, not yet
end-to-end bounded-memory snapshot persistence or a hard RSS guarantee.
The next step is native domain serializer migration followed by a streaming
SnapshotStore writer with incremental checksum and atomic replacement.


### Capture chunk ceiling

SnapshotCoordinatorLimits now provides max_capture_chunk_bytes (default
64 KiB). Every callback chunk is rejected when it exceeds this ceiling,
before aggregate append. A zero setting uses the default. The conformance test
uses a smaller configured limit to verify oversized chunk rejection. This is a
per-callback chunk size guard and does not imply the domain serializer itself
uses bounded allocations; end-to-end streaming and RSS measurement remain
pending.


### Provisional 200 MiB edge memory budget

ResourceBudgetLimits defaults to a provisional edge-device profile, also
available explicitly as temporary_edge_200mb(), with a combined logical byte
ceiling of 200 MiB across ResidentBytes and TransientBytes. The per-category ceilings are also 200 MiB, while the
combined check prevents the two categories from independently consuming that
amount. Other count-based resource limits remain caller-configured.

The aggregate reservation check is atomic under the ResourceBudget mutex and
returns RESOURCE_TOTAL_BYTE_BUDGET_EXCEEDED when the combined ceiling would be
crossed. The snapshot exposes aggregate logical byte usage and its configured
limit. This is admission accounting only: it does not include uninstrumented
allocations, allocator fragmentation, shared libraries, stacks, mapped files or
OS overhead, and it is not a process RSS limiter. The 200 MiB value is a
provisional engineering budget, not a verified operating limit. Target-device
RSS and sustained-load tests are required before treating it as a hard cap.


### Capture staging reservation against the shared memory budget

ResourceLease supports atomic incremental growth. SnapshotCoordinator capture
now reserves TransientBytes before adding the bundle header, domain framing and
each serialized chunk, then expands the reservation for the known envelope
encoding peak before encoding. The reservation is released on success and all
failure paths through RAII. A shared budget rejection stops capture before
the next append and returns the underlying resource-budget error. This
integrates snapshot capture with the provisional aggregate 200 MiB accounting
when a ResourceBudget is supplied. It still accounts logical serialized
buffers only; it cannot observe allocations internal to domain serializers,
SnapshotCodec, the standard library or the allocator, and is not a hard RSS
enforcement mechanism.


### Incremental SHA-256 foundation

Sha256Hasher now supports arbitrary chunk boundaries with a fixed 64-byte
input buffer and fixed compression state. It does not retain previously
submitted input. Finalization is idempotent and rejects subsequent updates;
invalid null input and length overflow are rejected. The legacy sha256_hex
helper delegates to this implementation. Conformance covers standard empty
and abc vectors, multi-boundary chunk updates, repeated finalization and
invalid input. SnapshotCodec and SnapshotStore have not yet been migrated to
consume the incremental interface, so this foundation alone does not remove
their full-buffer materialization.
