# SURGE 基礎設計 v1.0
### SnapshotStore authenticated streaming restore

SnapshotStore::load_stream provides a bounded-memory restore transport. It
first scans the complete selected generation in fixed-size chunks and verifies
the SHA-256 footer before invoking the payload sink. It then parses the bounded
metadata header and delivers the payload in chunks no larger than 64 KiB.
Corrupt or truncated primary generations are rejected before sink delivery and
the verified .bak generation is attempted instead. If sink delivery has
started and then fails, backup replay is not attempted because the sink may
already contain a staged prefix; the caller must abort its staging transaction.

The sink must be transactional or write to detached staging. It MUST NOT
publish restored live state during chunk delivery. The caller may publish only
after load_stream returns success and its own domain-level validation and
prepare phase succeeds. The existing load() API remains available and still
materializes the complete file for compatibility. The streamed API bounds
transport buffers, not domain restore allocations or process RSS. One writer
per snapshot path remains required.

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

Membership is a retention and context-placement guarantee, not a claim that
the member's semantic relationships are already understood. Admission MUST
NOT wait for complete classification, a resolved topic, a known dependency,
or a fully populated relation network. When no justified cluster is known,
the Core may place the information in an unresolved ChaosCluster. That
provisional membership is sufficient for safe retention and later discovery.
Subsequent events may add, revise, or remove memberships and may grow typed
relations as evidence accumulates. The Core MUST preserve provenance and
history for those changes; it MUST NOT invent semantic edges merely to make
the graph appear complete.

The parent cluster, child cluster, member and membership edges are committed
in one SemanticDelta or equivalent Core-owned transaction. Retry is
idempotent. Replay reconstructs identical cluster IDs and memberships.

The producer (adapter, external LLM, classifier or upstream runtime) is the
authority for classification labels and membership weights. The producer MUST
supply the classification and its weight as part of the streaming admission
envelope. SURGE MUST preserve these values exactly and MUST NOT infer, replace,
renormalize, calibrate, rank or silently override them. SURGE validates only
structural constraints (for example, finite normalized weight in [0,1]),
deterministic identity, parent existence and transaction integrity. A rejected
value is an explicit admission error, not an invitation for SURGE to guess.

A classification is a membership assignment, not a truth assertion. One input
may carry multiple classifications and weights and therefore belong to
multiple clusters in the same transaction. The weight is stored on the
member-specific Contains edge, not on the cluster, because different members
may have different upstream scores. Parent/child hierarchy edges remain
neutral and MUST NOT inherit a member score. Revisions append a new
classification/membership observation with provenance; they do not rewrite
the original source event. Classification labels and weights remain
replaceable upstream policy outputs, while the Core owns durable placement,
history, replay, resource admission and later graph evolution.

Adapters MUST NOT maintain a parallel context/memory graph or bypass the
Core membership gateway.
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
  through one Core Admission Gateway that preserves producer-supplied
  classification labels and member weights;
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
counterpart now stage an explicit removal of an existing Cluster/ChaosCluster`Contains` edge. The journal records `RELATION_REMOVE` so replay
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
integrates snapshot capture with the provisional aggregate 200 MiB accountingwhen a ResourceBudget is supplied. It still accounts logical serialized
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


### Snapshot validation buffer reduction

SnapshotCodec now verifies the envelope digest incrementally over the caller-owned
byte range and parses header fields directly from that buffer. The validation
path no longer creates a second full-size copy of the content preceding the
SHA-256 footer. The parser bounds every field read to the authenticated content
region, excluding the digest footer. Decode still allocates the requested
payload in its output document, and SnapshotStore still loads a complete file
into memory. This is a validation peak reduction, not yet streaming file I/O
or an end-to-end RSS guarantee.


### SnapshotStore chunked save API

SnapshotStore exposes save_stream(metadata, payload_bytes, reader, error). The reader yields non-empty chunks of at most 64 KiB with done=false, then must return one final empty chunk with done=true. The writer emits the existing version-1 envelope header, streams payload bytes to a same-directory temporary file while incrementally hashing the exact header+payload bytes, appends the 64-character SHA-256 footer, syncs the file, and uses the existing atomic replacement and backup-generation path. It rejects early EOF, extra bytes, oversized chunks, and payload lengths that do not match the declared length. The streamed path conservatively rejects equal state-version/journal-sequence watermarks because it cannot compare payload equality without materializing the new generation. Existing save/load APIs remain unchanged. The old generation is still loaded into memory for watermark validation, and load() still materializes the file. The authenticated load_stream API and SnapshotCoordinator::restore_from_store now provide a separate bounded-chunk restore path; see the streaming coordinator restore section below.


### Authenticated streaming restore through SnapshotCoordinator

SnapshotStore::load_stream verifies the complete envelope digest before it
delivers payload bytes and publishes the authenticated header metadata before
the first sink callback. SnapshotCoordinator::restore_from_store now consumes
the bundle incrementally: it parses the domain count and length-prefixed
domain/schema/payload frames across arbitrary 64 KiB chunk boundaries, checks
the registered domain order and schema IDs, and prepares one domain at a time.
It does not materialize the complete snapshot envelope or aggregate bundle.
The current domain payload is bounded by max_domain_payload_bytes; the
transient reservation accounts for the payload and its parser copy, plus the
actual journal-tail staging bytes. All prepared domains remain detached until
the complete bundle is parsed and the journal watermark is rechecked, after
which their noexcept commits are published.

The streamed coordinator path computes the SHA-256 digest of the complete
authenticated snapshot envelope during the verification pass, without
materializing the envelope. At an equal compacted-journal watermark, the
coordinator compares this digest with the journal checkpoint digest before
preparing domains. A mismatch fails closed; an exact match permits restore.
A checkpoint strictly older than the snapshot can proceed after context and
watermark validation. This path is not a hard RSS cap: prepared
domain state, journal event object overhead, allocator behavior and internal
domain allocations are not fully measured. The legacy byte-string restore
APIs remain available.


### Importance-driven retention and restoration

Cluster retention protects knowledge clusters and core focus/question/dependency anchors. Inactive non-knowledge clusters are prioritized for restoration before active low-priority clusters consume capacity. Retention transitions are journaled atomically; DORMANT/ARCHIVED lifecycle state is a compression policy, not deletion, and restoration requires the underlying canonical payload and recovery history to remain available. Tombstoned content is excluded from automatic restoration.


## 13. Implementation-to-specification gap review (2026-11-21)

This section reconciles normative v1.0 requirements with implementation evidence available on the current development branch. It is an implementation-status supplement, not a relaxation of the requirements. Code presence, unit conformance, CI completion, and measured target-device performance are distinct evidence levels. Items below marked partial or outstanding MUST NOT be described as complete.

### 13.1 Core admission and cluster graph

**Implemented foundations:** typed ContextGraph; deterministic cluster classification/membership helpers; atomic SemanticDelta; journal replay; budgeted C ABI mutation gateway; explicit cluster membership unlink; canonical membership-reference count; guarded Entity reclamation; Entity checkpoint compaction conformance.

**Gaps:** Core gateway is not mandatory for all C++, Python, ONNX, adapter, and recognition ingress. Some direct ContextGraph and question/status mutators can bypass shared admission/accounting. Existing legacy/unclustered data has no automatic provenance-preserving migration. Every ingress must eventually be audited and routed through the same transaction boundary.

### 13.2 Unified recognition and language runtime

**Implemented foundations:** Core-owned UnifiedDecision and C ABI candidate/surface mapping; bounded manifest metadata registry; session pinning and language-set switching; Japanese/English manifest fixtures; decision safeguards; language runtime logical resource reservations.

**Gaps:** UnifiedDecision is not fully integrated with the Python/ONNX recognition pipeline. LanguageRuntimeRegistry is metadata/session management, not a package loader. Digest/signature verification, safe package parsing, data-only resource readers, activation rollback, executable Japanese resources, functional English recognition and full morphology/lexicon/n-gram runtime remain outstanding. Existing legacy scoring paths still operate.

### 13.3 Budget enforcement and actual memory

**Implemented foundations:** mutex-protected logical ResourceBudget/ResourceLease; aggregate ResidentBytes+TransientBytes ceiling; C ABI budget profile; budgeted graph and journal/packet/snapshot facades; fail-closed admission and staging reservations; recovery accounting.

**Gaps:** budgets are opt-in and do not intercept every allocation or mutation path. Legacy C ABI open defaults to effectively unlimited ceilings for compatibility. Uninstrumented native, morphology, n-gram, ONNX, serializer, allocator, mapped-file, stack and OS costs are excluded. These budgets are accounting/admission safeguards, not hard process RSS limits. No sustained target edge-PC peak RSS, transient high-water, queue-depth, throughput or p50/p95/p99 latency report is yet evidenced.

### 13.4 Cluster retention, archive, restore, and reclamation

**Implemented foundations:** lifecycle records and transitions; versioned complete-candidate retention planning; guarded lifecycle execution; explicit journaled membership unlink; membership reference-count inspection; Entity-only fail-closed reclamation; verified cluster archive envelope/payload identity; streamed archive write and readback; detach planning for exclusive/shared Entity members; archive graph-record and Focus/Focus-history validation; ScopeSpan section structural and semantic validation (anchor identity, UTF8 byte offset basis, offset ordering, digest syntax, packet membership, target/antecedent existence, target-kind/effect consistency, metadata and enum bounds); Claim/Evidence validity-history validation and provenance closure; checkpoint compaction test for reclaimed Entity.

**Gaps:** archive payload validation validates identity, graph node/relation records, Focus/Focus-history, ScopeSpan structural and semantic invariants, and Claim/Evidence validity-history structure (owner kind, contiguous revisions, strictly increasing recorded state versions bounded by archive watermark, RFC3339 timestamp shape, evidence basis/review references, and Claim/Evidence basis binding through typed references or Supports relations). Archive closure explicitly retains validity-history basis and review evidence. ScopeSpan validation cannot re-run source-text digest/exact-text checks because the archive does not carry the original normalized source snapshot; those checks remain the responsibility of admission-time ScopeSpanValidator. Dependency evaluation sections are now structurally validated: unique record/delta identity, trigger Dependency kind and exact prerequisite/dependent binding, existing impacted-node references without duplicates, and rejected Dependency references without duplicates or overlap with triggered records. Dependency history closure retains trigger endpoints, impacted nodes and rejected dependencies. Temporal reassessment archives now validate per-Claim sequence continuity, strictly increasing recorded versions bounded by the archive watermark, RFC3339 reference/validity timestamps, evidence and relation references, SHA-256 snapshot digests, and previous-hash/chain-hash continuity. Temporal evidence nodes are retained in the archive closure. Question memory state, QuestionAspect status, and Claim lifecycle sections are validated for exact included-node coverage, unique owners, correct node kinds, and enum bounds. The parser rejects non-empty trailing records. A graph-and-current-Focus restore path now stages all archive nodes and relations, checks existing-ID identity conflicts and dangling references, and commits graph additions plus the current Focus through one BudgetedContextGraph semantic transaction. Existing identical Focus is retained without creating a duplicate journal mutation. Historical Focus entries are validated and consumed but are not replayed because the current SemanticDelta event model cannot preserve their original state-version stamps. Repeating an already-applied archive is a no-op; conflicts and stale target versions fail before mutation. Conformance tests cover successful graph restore, current Focus restore, idempotent replay, conflicting node identity, and stale-target rejection while asserting that the live graph version and content remain unchanged on rejection. Exact same-watermark archive verification is permitted only when streamed readback bytes match the requested payload; same-watermark replacement with different bytes remains prohibited. Retention advances an over-capacity DORMANT cluster to ARCHIVED on a subsequent pass and rejects pinned/knowledge clusters that are already ARCHIVED or TOMBSTONED. This paragraph describes the earlier graph/current-Focus-only restore increment and is superseded by the Full ClusterArchive v2 history restore implementation recorded in LIBRARY_ROLLOUT_GATE_v0.1.md (2026-11-21). The current importer restores all serialized ContextGraph history domains in one durable journal transaction and replays them after restart. Independent EvidenceProvenance/ChaosCluster snapshot domains remain governed by SnapshotCoordinator, not by the ContextGraph archive payload. Archive-file publication plus graph detachment remains a staged two-resource operation. The archive restore API returns a verified record but does not perform a complete transactional graph rehydrate. Archive-and-detach is limited to eligible Entity membership and does not implement general cluster tombstoning, typed-node reclamation, complete cross-domain reference closure, or a universal retention/archive workflow. Archive-specific limits do not represent a global memory cap.

### 13.5 Snapshot and journal memory behavior

**Implemented foundations:** incremental SHA-256; snapshot capture staging guard; chunked SnapshotDomain capture API; chunk ceiling; streamed snapshot save; authenticated streamed load; incremental SnapshotCoordinator domain parsing/preparation and detached publish; journal checkpoint compaction and backup pruning safeguards.

**Gaps:** Built-in domain serializers have not all migrated to capture_chunks. Compatibility capture still materializes domain payloads; coordinator still materializes aggregate bundle/envelope in capture; legacy save/load APIs materialize. The stream restore path still allocates prepared domain states and journal-tail objects. These are bounded by configured limits but not proven by hard allocator/RSS enforcement. Crash injection and cross-platform behavior need CI and device evidence.

### 13.6 Conformance, tests, and release evidence

Required evidence is tracked independently: (a) focused unit tests for each validator and failure mode, (b) integration tests covering atomic admission, recovery, archive/restore, lifecycle and compaction, (c) latest-head CI on supported platforms and sanitizer jobs, and (d) target hardware sustained streaming benchmarks. A test fixture must use canonical serializer format and literal newline encoding; malformed fixtures must fail for the intended invariant, not due to malformed escaping. The expanded temporal/state-section fixtures require latest-head CI and sanitizer confirmation before archive validation can be considered verified.

### 13.7 Priority order for closing gaps

1. Repair and canonicalize archive conformance fixtures; run archive tests and latest-head CI.
2. Expand archive parser validation to every serialized section, with exact counts, identity/reference closure, enum/state bounds, no trailing records, and tests per malformed section.
3. Implement detached, transactional archive rehydration and rollback-safe publication; verify replay/idempotency and shared-member safety.
4. Audit all ingress/mutators and route remaining production paths through the Core cluster/budget gateway; fail closed where admission is unavailable.
5. Integrate UnifiedDecision and Language Runtime into end-to-end Python/ONNX recognition; remove or explicitly isolate legacy parallel scoring.
6. Migrate built-in snapshot domain capture to streaming serializers and reduce aggregate/envelope materialization.
7. Add cross-platform crash/fault-injection tests and then measure target-device RSS/latency/throughput under sustained load.

These priorities are a sequencing recommendation. Completing an API or test alone does not close its end-to-end requirement; each item needs the evidence stated above.


## External Cognition Observation API

The Core MUST expose the committed cognition state to external SURGE models and
integrations through a read-only observation interface. Observation is a
first-class Core capability, not a debug-only side channel.

The C++ contract is `ContextGraph::observe_cognition()`, also exposed
through `BudgetedContextGraph::observe_cognition()`. The same observation is
available through the stable C ABI function
`surge_context_graph_observe_cognition()` as bounded JSON and through the
Python `SemanticCoreBridge.observe_cognition()` adapter. All forms return a detached
snapshot with the committed state version, canonical typed nodes, typed
relations, all active cluster memberships and their producer-supplied weights,
unresolved ChaosCluster memberships, active Questions and Interests, current
Focus and Focus history, unresolved scope count, and dependency-evaluation
count. The detached snapshot cannot mutate the graph and is consistent with
the graph state version captured by the observation.

The observation contract MUST satisfy:

1. **Faithfulness:** report only state actually committed in SURGE. Do not
   invent a classification, relation, attention target, hypothesis, confidence,
   or inferred meaning to make the view appear complete.
2. **Read-only isolation:** observing state MUST NOT change graph version,
   journal sequence, membership, focus, question state, or resource accounting.
3. **Provenance and uncertainty:** producer-owned classification labels and
   weights remain identifiable as such. Unknown or unrepresented dimensions
   remain unknown; they MUST NOT be serialized as zero-confidence or negative
   evidence.
4. **Closed-loop use:** an external model may inspect an observation, perform
   its own inference/classification, then project a new typed admission or
   membership delta back through the Core Admission Gateway. Observation itself
   never commits that inference.
5. **Versioned inspection:** every observation carries the committed state
   version so external consumers can detect stale observations and correlate
   their next projection with the state they inspected.
6. **No bypass:** external consumers receive a read-only projection, never
   mutable references to Core-owned graph storage.

The observation model is intentionally distinct from a future inference trace.
When a model performs mask estimation, contextual completion, candidate
ranking, or hypothesis generation, those are reported only when the relevant
model actually supplies a trace record. The Core MUST distinguish observed
committed state, externally supplied inference, and Core-derived structural
facts. A missing inference trace MUST NOT be fabricated by the observation
layer.

The current implementation provides detached graph-state snapshots
through C++, C ABI and Python, with bounded-buffer handling and read-only
semantics. A versioned incremental observation stream, inference trace
ingestion, and model-specific mask-estimation telemetry remain integration
work and MUST NOT be claimed as implemented until independently tested.


## SURGE Model: Associative Cognition Candidate Recall and Growth

### 1. Purpose

The SURGE Model MUST do more than place incoming information into a
preselected cluster. For each incoming information item, it SHOULD recall
multiple possible cognition states from existing memory. A cognition state
candidate describes a possible contextual interpretation of the new input
through already retained clusters, members, relations, Questions, Interests,
Focus and historical classification observations.

The candidate set is a first-class, externally observable output of the
cognition loop. It is not a committed classification and MUST NOT be confused
with a truth assertion.

### 2. Separate producer-owned admission from model-owned recall

Two responsibilities MUST remain distinct:

- **Admission classification:** the upstream producer (external model,
  classifier or adapter) supplies classification labels and membership
  weights. Core validates structural integrity and preserves the supplied
  values and provenance. Core MUST NOT generate, replace, normalize or
  silently revise these admission values.
- **Cognition candidate recall:** the SURGE Model searches and associates
  existing memory to produce a plural set of possible cognition states for
  the incoming item. It may use historical classifications and weights as
  evidence for retrieval and candidate ordering, but those candidates remain
  hypotheses and MUST NOT silently become admission memberships.

This separation is normative. Candidate recall does not violate the
producer-owned classification contract because recall is a proposal about
possible context, not an authoritative input classification.

### 3. Recall inputs and associative evidence

Candidate recall SHOULD consider, where available:

- lexical, token, n-gram and semantic references attached to the input;
- prior members and clusters sharing those references;
- historical producer-supplied classification labels and membership weights;
- co-occurrence, distance, sequence and recurrence patterns;
- typed relations, including agreement, conflict, support, contradiction,
  dependency, reference and supersession;
- temporal validity, source provenance and revision lineage;
- current and historical Focus, active Questions and Interests;
- unresolved and exploratory members retained in ChaosClusters;
- previously recalled candidates and their later confirmation, rejection
  or revision outcomes.

The model MUST be able to return multiple candidates, including candidates
from different clusters. It MUST preserve ambiguity where the evidence
supports more than one interpretation. An empty candidate set is valid and
must not prevent the input from being retained through the admission path.

### 4. Self-accumulation and model growth

The SURGE Model grows by accumulating the history of producer-supplied
classifications, membership weights, relations and subsequent outcomes in
the canonical cluster graph. These observations become reusable associative
evidence for future recall.

Growth therefore means an expanding and improving set of retrievable
associations and cognition-state candidates, not an unconstrained mutation
of model parameters and not an authority transfer over admission labels.

The system SHOULD retain:
- the original producer label and weight, unchanged;
- producer identity, model/policy version and observation time;
- the input/member and target cluster identities;
- the candidate set and the memory version used to recall it;
- later confirmation, rejection, correction or unresolved outcome, with
  source and time provenance.

Historical observations MUST NOT be overwritten when later evidence changes
the current association. A new observation or revision is appended. Recall
must be reproducible against a pinned memory version and policy version.

### 5. Candidate lifecycle and feedback

A candidate SHOULD carry at least:
- candidate ID and input/member reference;
- candidate cognition-state or target cluster reference;
- supporting memory references;
- relation/path evidence explaining why it was recalled;
- memory state version and recall policy/model version;
- candidate score or rank with its producer and scoring provenance;
- uncertainty/ambiguity status;
- lifecycle status such as proposed, externally-confirmed,
  externally-rejected, superseded or unresolved.

A candidate score is not an admission membership weight. The two values
MUST use distinct fields and semantics. SURGE MUST NOT copy a candidate
score into a Contains edge unless an authorized upstream producer explicitly
projects a classification and membership weight through the admission API.

External models may inspect candidates and the underlying cognition
observation, then project a new classification, weight, Question, Interest,
Focus change or relation back to Core. That feedback is a new versioned
transaction and MUST preserve the external producer's provenance.

### 6. Observation API integration

The cognition observation surface SHOULD expose both:
1. the exact committed Core state; and
2. separately identified recall-candidate records, including their supporting
   references and version pins.

The committed-state snapshot MUST remain read-only. Candidate generation
MUST NOT mutate the graph. Candidate outcomes and externally projected
feedback are committed through explicit mutation APIs.

The API MUST make it impossible to mistake a recalled candidate for a
committed cluster membership. Snapshot and candidate records must identify
their respective state/version and status.

### 7. Growth and quality evaluation

Model growth MUST be evaluated by more than memory size. Conformance and
benchmarking SHOULD measure:
- candidate recall coverage and candidate diversity;
- top-k recall and ranking quality against externally reviewed outcomes;
- ambiguity preservation and false-positive candidate rate;
- recovery of long-range and cross-cluster associations;
- improvement as classification/weight history accumulates;
- stability under contradictory, revised and time-expired evidence;
- deterministic replay at a pinned memory version;
- latency, transient memory and resident memory under streaming load.

A larger candidate set is not automatically better. The model must improve
useful recall while retaining uncertainty, provenance, bounded resource use
and the distinction between proposals and committed cognition state.

### 8. Implementation boundary

The current cognition observation snapshot reports committed graph state.
It does not by itself implement associative candidate generation,
candidate lifecycle storage, candidate feedback learning or candidate-quality
evaluation. Those capabilities require explicit runtime components and tests.
They MUST NOT be claimed complete merely because graph observation is
available.


### Associative recall and feedback implementation increment

ContextGraph now exposes `recall_cognition_candidates(input_member_id,
query_terms, max_candidates)`. The current implementation performs deterministic
literal substring matching over retained node content and reference strings,
aggregates matching member evidence by existing Cluster/ChaosCluster
membership, returns matched terms and supporting member IDs, pins the
observation state version, and orders candidates by a retrieval-only score
with stable cluster-ID tie breaking. This is a lightweight baseline recall
implementation, not semantic embedding or a learned classifier. Query terms
are supplied by the caller; SURGE does not infer admission classifications.

Candidate recall is read-only and does not mutate Contains memberships or
admission weights. Candidate recall scores are distinct from producer-owned
membership weights and MUST NOT be projected into Contains edges implicitly.

The Core also exposes `append_cognition_feedback`, which stages a typed
Evidence record containing candidate outcome and producer/model/policy
provenance and links it to the candidate cluster. The budgeted facade
provides recall and feedback staging/commit entry points. Feedback outcomes
are append-only observations and do not automatically alter classifications,
candidate scores, or graph membership.

Implementation scope at this increment: candidate recall and feedback
persistence foundations. Native semantic candidate generation, candidate
history querying/serialization through the cognition observation API,
feedback-driven recall adaptation, C ABI/Python exposure, and automated
quality benchmarks remain follow-up integration tasks.


## Cognition recall and feedback regression contract (2026-11-22)
- Candidate recall remains a read-only projection over the committed graph. Query terms are supplied by an external producer; Core does not infer the classification of the incoming input.
- Recall ranking combines matched member content/references, upstream membership weights, bounded within-member term proximity, cluster classification/partition labels, and one-hop existing inter-cluster relations (agreement, support, dependency, contradiction and conflict).
- Inter-cluster propagation is bounded to one hop and is not recursively amplified. Relation coefficients are retrieval heuristics, not semantic truth or membership weights.
- Recall scores are ranking-only. They are never persisted as Contains weights, never alter graph state, and never authorize cluster admission.
- External feedback is appended as Evidence in a caller-owned transaction. It preserves the producer, producer version, policy version, timestamp, outcome and notes. Feedback does not directly modify existing membership or automatically train recall weights.
- Regression coverage verifies candidate ranking with distinct upstream weights, snapshot version, unchanged Contains weights, append-only feedback, invalid outcome rejection and unchanged membership counts.
- The current baseline is still lexical and graph-structural, not embedding-based. Feedback history querying/adaptive learning, C ABI/Python exposure, and independently annotated recall-quality benchmarks remain future work.
- GitHub Actions can measure CI-runner resource use, but it cannot substitute for target-device RSS/latency measurements. Target qualification requires running the benchmark harness on the intended hardware with fixed input stream, warm-up, duration, RSS sampling and p50/p95/p99 latency capture.
