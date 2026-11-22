# SURGE Core API v0.2
## Core Contract for SURGE Specification v0.4

**Version:** 0.2.0  
**Applies to:** SURGE v0.5.0  
**Status:** Normative API Design  
**Transport:** Transport-neutral (HTTP / RPC / in-process ABI mapping is implementation-specific)

---

## 1. Purpose

This document defines the language- and transport-independent contract between Input Adapters, Recognition Engines, and SURGE Core.

The Core API separates:
- input acceptance and immutable Evidence registration;
- recognition proposals from committed state;
- validation from atomic commit;
- bounded reconciliation from ordinary updates;
- read access from state mutation.

The JSON payload contract is defined by `schemas/surge-v0.5.schema.json`. Cross-record invariants and lifecycle rules remain Core Validator responsibilities.

## 2. Authority and trust boundaries

### 2.1 Core authority

Only the Core Transaction Coordinator may create or change committed Context State. Recognition Engines, LLMs, and Adapters may submit Events or Proposals but MUST NOT write directly to the Repository or mark their own output as committed.

### 2.2 Input trust

All adapter payloads and model proposals are untrusted input. The Core validates schema, provenance references, producer identity, version, scope, and permitted operations. A valid JSON Schema instance is not necessarily a semantically valid state change.

### 2.3 Immutable records

Input Events and Evidence are append-only. Corrections, retractions, and revisions are represented by new Events with explicit references to superseded records. A successful operation never silently rewrites prior Evidence or committed history.

## 3. Common conventions

### 3.1 Identifiers

All `*_id` and `*_ref` values are opaque, non-empty strings. Clients MUST NOT infer ordering or meaning from identifier contents.

### 3.2 Versions

`state_version` is a monotonically increasing integer within a Context. A mutation proposal MUST identify the Context version on which it was based. The Core MUST reject or rebase a stale Delta; it MUST NOT blindly overwrite a newer state.

### 3.3 Idempotency

Every mutating operation accepts an idempotency key. The key is scoped by Context and operation family. Repeating the same key with the same canonical request returns the original logical result. Reusing the key with a different request returns `IDEMPOTENCY_KEY_REUSED`.

### 3.4 Time

All machine timestamps use RFC 3339 / ISO 8601 with an explicit timezone. Unknown time is represented by the Schema `timeValue.status = UNKNOWN`, not by a fabricated timestamp or zero date.

### 3.5 Pagination

Collection reads use opaque cursors. A cursor is bound to its query and snapshot version. Clients MUST NOT construct or modify cursors.

### 3.6 Result envelope

Successful calls return:
- `request_id`
- `status`: `OK`, `ACCEPTED`, `PARTIAL`, or `CONFLICT`
- `context_id`
- `state_version` when a committed version exists
- `result`
- `warnings` (possibly empty)

Errors return:
- `request_id`
- `error.code`
- `error.message`
- `error.retryable`
- `error.details` (optional structured context)

## 4. API operation summary

| Operation | Access | State effect | Purpose |
|---|---|---|---|
| `ingest_event` | Write | Append Event/Evidence | Accept normalized input |
| `get_context` | Read | None | Read a consistent Context View |
| `submit_proposal` | Write proposal | No committed state change | Submit recognition candidates |
| `validate_delta` | Validate | None | Check Delta against a state version |
| `commit_delta` | Core write | Atomic commit | Commit a validated Delta |
| `reconcile` | Schedule | Async state changes through Core | Reevaluate impacted graph |
| `get_question` | Read | None | Read Question and lineage |
| `get_focus` | Read | None | Read Focus for a Scope |
| `create_snapshot` | Core/admin | Append snapshot metadata | Create recoverable snapshot |
| `restore_snapshot` | Admin only | Controlled recovery | Restore from validated snapshot + log |

---

## 5. Operation contracts

### 5.1 ingest_event

**Purpose:** Accept an Input Event, deduplicate it, and register immutable Event/Evidence records.

**Request**
- `context_id`
- `event`: Schema `inputEvent`
- `idempotency_key`
- `expected_state_version` (optional; used when caller requires a specific Context view)

**Processing**
1. Validate Event schema and required source ordering information.
2. Validate idempotency key.
3. Append Event without modifying earlier Events.
4. Register Evidence with Event provenance and integrity digest.
5. Return accepted Event/Evidence references.
6. Enqueue recognition asynchronously; recognition failure does not remove the accepted Event/Evidence.

**Response result**
- `event_ref`
- `evidence_refs`
- `duplicate`
- `recognition_job_ref` (optional)

Duplicate delivery MUST NOT create duplicate Evidence or duplicate state commits. If the Event is accepted but recognition is pending, return `ACCEPTED`, not a fabricated completed state.

### 5.2 get_context

**Purpose:** Read a consistent Context View.

**Request**
- `context_id`
- `at_state_version` (optional)
- `include`: selected collections (units, entities, clusters, relations, dependencies, questions, interests, focus, provenance)
- `filters` (optional)
- `page_size` and `cursor` (optional)

**Response result**
- `context_state`
- requested object collections
- `next_cursor` (optional)
- `view_state_version`

A read MUST be internally consistent at one state version. If the requested historical version is unavailable, return `STATE_VERSION_NOT_FOUND`.

### 5.3 submit_proposal

**Purpose:** Submit a Recognition Proposal. Submission is not approval or commit.

**Request**
- `context_id`
- `proposal`: Schema `proposal`
- `idempotency_key`

**Processing**
1. Authenticate producer identity and check producer/version policy.
2. Validate Proposal schema and referenced Event/Evidence existence.
3. Check that `context_snapshot_version` is available.
4. Store the Proposal as an immutable candidate record.
5. Return a proposal status: `PENDING_VALIDATION`, `REJECTED`, or `NEEDS_REBASE`.

**Response result**
- `proposal_ref`
- `proposal_status`
- `validation_job_ref` (optional)
- `rejection_reasons` (optional)

A Proposal MUST NOT change committed Context State. The Core may accept only a subset of its candidates.

### 5.4 validate_delta

**Purpose:** Validate a State Delta against an explicit base state.

**Request**
- `context_id`
- `delta`: Schema `stateDelta`
- `validation_mode`: `STRICT` or `DRY_RUN`

**Validation**
- Schema and operation shape
- Base State Version availability
- read_set consistency
- write-set permission
- referenced ID existence
- Evidence/Provenance traceability
- legal lifecycle transitions
- Question resolution/reopen basis
- Interest/Focus separation
- Dependency Impact validity
- idempotency and duplicate operations
- applicable Invariants D-01 through D-20 and I-01 through I-25

**Response result**
- `valid`
- `base_state_version`
- `current_state_version`
- `conflicts`
- `violations`
- `normalized_delta_ref` (optional)
- `validation_token` (optional, short-lived and bound to the exact Delta + version)

Validation is advisory until commit. The Core MUST revalidate at commit time.

### 5.5 commit_delta

**Purpose:** Atomically commit an authorized State Delta.

**Request**
- `context_id`
- `delta_ref` or inline `delta`
- `expected_state_version`
- `idempotency_key`
- `validation_token` (optional optimization; never bypasses revalidation)

**Preconditions**
- Caller is authorized to request Core commit.
- Delta producer is permitted to propose its operations.
- Base version and read_set are consistent.
- All referenced records exist.
- All applicable invariants pass.
- No duplicate logical commit exists.

**Atomic behavior**
1. Revalidate the Delta against the current version.
2. Apply the complete write set and append commit/history events atomically.
3. Increment Context state_version exactly once for the logical transaction.
4. Return committed references and the new version.

If any operation fails, none of the Delta's state changes are exposed as committed. A partial commit is forbidden.

**Response result**
- `commit_id`
- `committed_state_version`
- `committed_refs`
- `history_event_refs`
- `reconciliation_job_ref` (optional)

A stale version returns `STATE_VERSION_CONFLICT` with current version and conflicting refs. It MUST NOT silently overwrite.

### 5.6 reconcile

**Purpose:** Schedule or continue bounded reevaluation for a known root change or impacted target set.

**Request**
- `context_id`
- `root_change_refs`
- `base_state_version`
- `scope_refs` (optional)
- `policy_version`
- `idempotency_key`
- `limits` (optional implementation policy: max iterations, max nodes, deadline)

**Processing**
1. Build Impact Set from active Dependency edges and trigger conditions.
2. Identify SCC cycle groups.
3. Schedule deterministic generation work.
4. Evaluate each node at most once per generation/effective input fingerprint.
5. Iterate to fixed point or stop condition.
6. Commit only validated deltas through Transaction Coordinator.
7. Persist stop reason and unresolved work.

**Response result**
- `reconciliation_id`
- `status`: `PENDING`, `RUNNING`, `CONVERGED`, `STALLED`, `RESTART_REQUIRED`
- `generation`
- `scheduled_count`
- `visited_count`
- `stop_reason`
- `resulting_state_version` (optional)

Reaching a configured limit is not success. The job MUST report `STALLED` or an equivalent non-converged state and MUST NOT promote provisional results to committed certainty solely because the limit was reached.

### 5.7 get_question

**Purpose:** Read a Question and its lifecycle context.

**Request**
- `context_id`
- `question_id`
- `include_lineage` (optional)

**Response result**
- Question record
- related Question refs
- trigger/Evidence refs
- Interest refs
- resolution basis or supersession lineage

This is read-only. Reopening is performed through a State Delta and `commit_delta`, never by direct mutation.

### 5.8 get_focus

**Purpose:** Read active and historical Focus records for a Scope.

**Request**
- `context_id`
- `scope_id`
- `include_history` (optional)

**Response result**
- active Focus records
- Scope policy
- transition history
- related Interest refs

This is read-only. Focus changes are proposed and committed through Core State Delta.

### 5.9 create_snapshot

**Purpose:** Create a recoverable snapshot of a committed Context version.

**Request**
- `context_id`
- `state_version` (optional; defaults to current committed version)
- `idempotency_key`

**Response result**
- `snapshot_id`
- `state_version`
- `event_offset`
- `schema_version`
- `integrity_digest`
- `status`

Only committed state may be snapshotted. Snapshot is derived data; Event Log remains the reconstruction authority.

### 5.10 restore_snapshot

**Purpose:** Controlled administrative recovery.

**Request**
- `context_id`
- `snapshot_id`
- `replay_to_event_offset` (optional)
- `recovery_mode`: `VERIFY_ONLY` or `RESTORE_NEW_GENERATION`
- `idempotency_key`

**Processing**
1. Verify snapshot digest and schema compatibility.
2. Replay required Event Log range.
3. Verify commit sequence and state version.
4. Isolate invalid/corrupt records.
5. Publish restored state only after validation.

Restore MUST NOT erase or rewrite the original Event Log. Recovery creates a new recovery generation and records the source snapshot, replay range, validation result, and operator identity.

---

## 6. State transition API rules

### 6.1 Question

- SUPERSEDED is terminal.
- RESOLVED -> OPEN requires new Evidence or a recorded change in validity/reassessment that re-establishes the unresolved condition.
- RESOLVED requires a resolution_basis.
- Duplicate Questions are linked or merged non-destructively; ambiguous similarity does not authorize merge.

### 6.2 Interest

Interest changes do not themselves commit a Claim, Evidence, Entity identity, or Cluster structure. Interest may remain active after a Question resolves when its target is independently relevant.

### 6.3 Focus

Explicit Focus instructions take precedence over generic hysteresis. Focus is Scope-bound. Multiple Focus records are allowed according to Scope Policy. Focus updates MUST NOT implicitly mutate Cluster membership.

### 6.4 Dependency

Dependency direction is prerequisite -> dependent for impact traversal: a change to prerequisite B schedules dependent A for reevaluation when A depends on B. API field names must preserve this semantic even if storage uses another orientation.

### 6.5 Entity and Cluster lineage

Merge/split operations create new lineage events and preserve old IDs. Historical references remain resolvable.

---

## 7. Error model

| Code | Meaning | Retry |
|---|---|---|
| SCHEMA_VALIDATION_FAILED | JSON Schema mismatch | No, correct request |
| SEMANTIC_VALIDATION_FAILED | Cross-record or invariant violation | No, revise proposal |
| STATE_VERSION_CONFLICT | Base/read version is stale | Yes, re-read and regenerate |
| IDEMPOTENCY_KEY_REUSED | Same key used for different payload | No, use a new key |
| REFERENCE_NOT_FOUND | Referenced record does not exist | Depends on delayed reference policy |
| EVIDENCE_PROVENANCE_MISSING | State cannot be traced to Evidence | No, add valid provenance |
| ILLEGAL_STATE_TRANSITION | Lifecycle transition is not allowed | No |
| QUESTION_RESOLUTION_BASIS_REQUIRED | Resolve request lacks basis | No |
| DEPENDENCY_CYCLE_STALLED | SCC failed to converge within policy | Later re-evaluation may retry |
| RECONCILIATION_LIMIT_REACHED | Node/iteration/deadline limit reached | Policy-dependent |
| PRODUCER_NOT_AUTHORIZED | Producer lacks requested capability | No |
| CONTEXT_NOT_FOUND | Context does not exist | No |
| STATE_VERSION_NOT_FOUND | Requested historical version unavailable | No |
| SNAPSHOT_INTEGRITY_FAILED | Snapshot digest/schema invalid | Use another verified snapshot |
| RECOVERY_VALIDATION_FAILED | Replay/commit sequence inconsistent | Isolate and investigate |
| INTERNAL_ERROR | Unexpected Core failure | Retry only if marked retryable |

Errors MUST distinguish permanent invalidity from transient infrastructure failure.

---

## 8. Concurrency and transaction semantics

1. All mutations are idempotent by operation family and Context.
2. Delta declares read_set and write_set.
3. Commit checks expected_state_version and read_set atomically.
4. A stale Delta is never force-applied.
5. Non-overlapping writes may be rebased only after semantic validation.
6. Commit event, state mutation, and history append are one atomic unit.
7. Duplicate Event delivery cannot create duplicate Evidence or duplicate logical commits.
8. Reconciliation jobs are idempotent by root changes, base version, policy version, and effective input fingerprint.
9. Read APIs return a consistent state version.
10. Snapshot creation and restore are version-pinned.

## 9. Security and isolation

- Context ID is the isolation boundary unless a deployment explicitly defines a broader shared namespace.
- Every operation requires authorization scoped to Context and operation.
- Producer identity and model version are recorded for Proposals.
- Raw Event content may be stored by reference; access control for the underlying payload remains the Adapter/Repository deployment responsibility.
- Logs MUST NOT silently expose content from another Context.
- Untrusted Proposal metadata is data, never executable instruction.

## 10. Transport mapping

This contract does not mandate HTTP paths or a specific RPC framework. A transport binding SHOULD map operation names to stable endpoints/methods while preserving:
- request/response fields;
- idempotency semantics;
- state version checks;
- error codes;
- atomic commit boundaries;
- async job status.

A future OpenAPI or protobuf binding may be generated from this contract without changing the Core semantics.

## 11. Conformance

A Core implementation conforms to this API design only if:
- Proposals cannot write committed state;
- Event/Evidence are append-only;
- commit_delta is atomic and version-checked;
- Question/Interest/Focus lifecycle boundaries are enforced;
- Dependency reconciliation has explicit convergence/stop semantics;
- all committed records retain provenance;
- Snapshot recovery is verifiable and non-destructive;
- schema and cross-record validation are both enforced.



## 12. v0.5 API Contract Additions

### 12.1 Semantic comparison
CoreはEXACT/EQUIVALENT/COMPATIBLE/CONFLICTING/INCOMPARABLEの比較結果を内部で保持する。収束成功はEXACT/EQUIVALENTに限り、canonicalization_versionとProjection参照を記録する。

### 12.2 Event correction
ingest_eventはcorrection_kind、revision_group_ref、revision_sequence、supersedes_event_ref、clarifies_event_refsを受け付ける。参照存在とRevision Lineageの非循環性を検証する。競合分岐はQuestionとして保持し、受信時刻だけで決着させない。

### 12.3 Focus policy
FocusはScope Policy内で評価する。遷移にはpolicy_version、decision_reason_codes、considered_candidate_refs、deferred_candidate_refsを記録する。解消不能な競合はPARTIAL/DEFERREDとして返す。

### 12.4 Temporal contract
Temporal Scopeは既定でHALF_OPEN。UNKNOWN、OPEN_BOUNDARY、point event、precisionを区別する。比較不能な時間はINCOMPARABLEとし、矛盾と断定しない。

### 12.5 Lineage and Evidence components
Source Lineage更新はDAG検証を通す。部分独立はComponent単位で評価し、同一Component fingerprintはClaim-side corroborationで一度だけ数える。UNKNOWNはINDEPENDENTとして扱わない。

### 12.6 Producer interoperability
submit_proposalはsemantic_contract_versionとcanonicalization_versionを検証する。未対応型はUNMAPPEDまたは明示的拒否理由を返し、暗黙Coerceしない。共通CalibrationなしのProducer Confidenceは比較不能とする。

### 12.7 Lifecycle and migration
Focus更新とCluster lifecycle更新は別操作とする。v0.4 payloadはschema_versionを書き換えるだけでv0.5に移行しない。Migrationは元Schema、Migration Policy、未解決Mapping、Provenanceを記録する。

### 5.11 observe_focused_cognition

**Purpose:** Read the externally observable cognition centered on the currently focused cluster.

**Response result**
- committed `state_version`
- current Focus and Focus history
- focused Cluster node
- direct member nodes and their membership weights
- relations whose endpoints are both in the focused projection

This operation is read-only. If no Focus exists, it returns an empty focused projection with the current state version. It does not infer truth or mutate Cluster lifecycle.

### 5.12 append_derived_cognition_cluster (Core library operation)

**Purpose:** Persist a newly composed recognition as a new Cluster.

The caller supplies the recognized content, a unique Cluster ID, and at least two existing or same-Delta source Cluster IDs. The Core validates source types and identifiers, adds the new Cluster, and records `DERIVED_FROM` relations from the new Cluster to each source Cluster in the same SemanticDelta. The operation does not judge whether the recognition is true. Focus selection is a separate operation and may target the new Cluster in the same transaction.



### 5.13 observe_focused_cognition (spatial overload, C++ library)

**Purpose:** Return the cognition information inside a 3D focus region.

**Inputs**
- `FocusRegion`: center and non-negative axis half-extents.
- `ClusterSpatialPosition[]`: cluster coordinates in the same graph coordinate space.
- `ClusterFocusDepth[]`: independent traversal depth per cluster.
- `max_nodes`, `max_relations`: deterministic work limits.

**Semantics**
- Region membership is evaluated against the 3D axis-aligned extent.
- Depth zero returns the cluster only; positive depth traverses Contains edges.
- A nested cluster uses its own depth setting, independently of its parent.
- The response contains every node and relation admitted by the region/depth/budget projection, plus work metrics and an explicit budget-exhausted flag.
- This explicit-argument overload is read-only and projects the caller-supplied region, positions and depths. It is useful for isolated projection, testing and compatibility.
- The persisted overload `observe_focused_cognition(max_nodes, max_relations)` reads the journaled FocusRegion, cluster positions and per-cluster depths from ContextGraph. It does not require the caller to reconstruct those inputs.
- The no-argument overload uses persisted spatial Focus when a region exists. If no spatial region has been configured, it falls back to the legacy single-cluster projection for source compatibility.
- The logical region membership is computed independently of node/relation traversal budgets. `logical_region_cluster_ids` remains complete while `processed_region_cluster_ids` and `deferred_region_cluster_ids` report actual execution. Budget exhaustion never silently contracts the logical region.
- `max_nodes` caps admitted graph nodes. `max_relations` caps combined relation inspection and traversal. Persisted observation uses a derived in-memory x-axis ordered index as a broad phase, followed by exact 3D membership checks. `position_checks` counts those exact checks after filtering. The index is invalidated by position updates, replay, archive import and snapshot publication/rollback; it is rebuilt lazily and is not durable state.
- Persisted observation also uses a derived outgoing-Relation adjacency index keyed by source cluster ID. It stores Relation IDs, is rebuilt lazily, and is invalidated by topology mutations, replay, archive import and snapshot publication/rollback. Each projection inspects outgoing Relations only for reached clusters; unrelated graph Relations do not consume its relation budget. `relations_examined` counts newly inspected outgoing records and `relations_traversed` counts Contains traversal steps. The initial index build remains O(E) with O(E) derived memory.
- This initial one-axis index is not a balanced 3D spatial tree. A wide x extent can still visit most positions and the initial index build is O(N). The explicit caller-supplied projection overload continues to inspect its supplied positions directly.
- Deferred roots are deterministic, but the current API does not expose a durable continuation cursor. A subsequent observation recomputes the projection from the beginning. These limits mean this API does not claim a strict total-CPU, wall-clock or process-RSS ceiling.


### 5.14 Durable spatial Focus updates

The following update methods append independent commands to a caller-owned
`SemanticDelta` and are committed through the normal ContextGraph journal:

- `append_focus_region_update(delta, region, error)` for initialization/replacement
- `append_focus_region_move(delta, center, error)` to change only the center
- `append_focus_region_resize(delta, half_extents, error)` to change only the extents
- `append_cluster_spatial_position_update(delta, position, error)`
- `append_cluster_focus_depth_update(delta, depth, error)`

Journal event kinds are `FOCUS_REGION_SET_V1`, `FOCUS_REGION_MOVE_V1`,
`FOCUS_REGION_RESIZE_V1`, `CLUSTER_SPATIAL_POSITION_SET_V1`, and
`CLUSTER_FOCUS_DEPTH_SET_V1`. Region movement changes only the center;
region resize changes only the half-extents. Each has an independent validation
and replay path.
A transaction may carry multiple position/depth updates, but a region update is
never encoded as a depth update or vice versa.

The `CGS2` ContextGraph snapshot stores the optional region, cluster positions and
cluster depths. Restore accepts `CGS1` for backward compatibility and replays the
three new event kinds in a journal tail.

### 5.15 C ABI spatial Focus

The additive C ABI is version 0.4 and exposes:

- `surge_context_graph_set_focus_region`
- `surge_context_graph_move_focus_region`
- `surge_context_graph_resize_focus_region`
- `surge_context_graph_set_cluster_spatial_position`
- `surge_context_graph_set_cluster_focus_depth`
- `surge_context_graph_observe_focused_cognition`

The observation API returns the logical region and its full geometric membership
separately from processed/deferred cluster roots. Its work object includes operation
counts, elapsed microseconds, configured limits and an explicit budget-exhausted flag.

### 5.16 Atomic ProjectionBatch C ABI

`surge_projection_batch_admit_and_apply()` accepts one RecognitionPacket, its normalized source bytes, ProjectionBatch producer/policy metadata, Evidence references, information nodes, weighted Relations, weighted cluster Memberships, and spatial placements.

The function converts the inputs to the same typed `ProjectionBatch` used by the C++ API, validates the envelope, prepares the SemanticDelta through `ContextGraph::prepare_events()`, and submits the packet and semantic journal events through `PacketAdmissionCoordinator::admit()`. Packet admission and graph projection are one EventJournal transaction. It does not create or move Focus as a side effect.

The additive public structures are `SurgeProjectionMetadataInput`, `SurgeProjectionRelationInput`, `SurgeProjectionMembershipInput`, and `SurgeProjectionSpatialPlacementInput`. The function returns duplicate status, resulting state version, packet status, and an error string.

The original `SurgeRecognitionPacketInput` layout and `surge_projection_batch_admit_and_apply()` entry point remain unchanged. ABI 0.6 adds `SurgeDocumentStreamInput` and `surge_projection_batch_admit_and_apply_v2()`. The v2 entry point accepts the optional document identity/generation, document sequence, and document-global UTF-8 byte range separately from packet-local offsets. Core persists this envelope in RecognitionPacket journal records using the backward-readable RPA3 encoding. Within each (context, document_ref, document_generation) stream, new packets must start at sequence 1 / byte offset 0 and then advance by exactly one sequence and contiguous byte range. Gaps, overlaps, sequence reuse, and incomplete document identity are rejected before commit. Existing callers may continue using the original ABI without document-global continuity enforcement.

The current native API accepts one source packet per ProjectionBatch. Raw JSON decoding is not embedded in the native library; `schemas/projection-batch-v0.1.schema.json` defines the JSON transport contract for callers to validate and decode before invoking the typed API.
