# SURGE Core API v0.3
## Core API with SURGE v0.8 Compact Recognition Extension

**Version:** 0.3.0  
**Applies to:** SURGE v0.6.0 base API + additive SURGE v0.8.0 packet interface  
**Status:** Normative API Design  
**Transport:** Transport-neutral (HTTP / RPC / in-process ABI mapping is implementation-specific)

---

**Normative precedence:** 本書のv0.6 API追加規定（第13章以降）は、継承したv0.2 APIのQuestion/時間/同期契約と異なる場合に優先する。


## 1. Purpose

This document defines the language- and transport-independent contract between Input Adapters, Recognition Engines, and SURGE Core.

The Core API separates:
- input acceptance and immutable Evidence registration;
- recognition proposals from committed state;
- validation from atomic commit;
- bounded reconciliation from ordinary updates;
- read access from state mutation.

The inherited JSON payload contract is defined by `schemas/surge-v0.6.schema.json`; the bounded RecognitionPacket contract is defined by `schemas/surge-v0.8-input.schema.json`. Cross-record invariants and lifecycle rules remain Core Validator responsibilities.

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
- applicable Invariants D-01 through D-34 and I-01 through I-35

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


## 13. v0.6 Question Aspect API Contract

### 13.1 Read model
get_question returns aspect_refs, Aspect records (or paginated refs), derived Question status, and aspect_aggregation_version. unresolved_aspects is a compatibility projection only and is not authoritative.

### 13.2 Aspect operations
Aspect mutations are State Delta operations and are committed only by commit_delta:
- ADD_ASPECT: create Aspect with parent Question and provenance.
- UPDATE_ASPECT_STATUS: legal lifecycle transition with reason.
- RESOLVE_ASPECT: add Resolution and evidence in the same atomic Delta.
- REOPEN_ASPECT: requires invalidated resolution basis or new Evidence.
- SPLIT_ASPECT: create child Aspects and Lineage Event atomically.
- MERGE_ASPECTS: create new Aspect with all parent refs and dependency candidates.

Core recalculates Question status after all Aspect operations in the Delta: all required Aspects INVALIDATED => INVALIDATED; all required Aspects terminal with at least one RESOLVED => RESOLVED; terminal and non-terminal Aspects mixed => PARTIALLY_RESOLVED; otherwise INVESTIGATING/DEFERRED/OPEN follow the normative aggregation order. Client-supplied Question status is rejected or ignored. Aspect dependency evaluation and Question aggregation use the same committed State Version.

### 13.3 resolve_question_aspect
Convenience request fields: context_id, question_id, aspect_id, resolution, expected_state_version, idempotency_key. It is semantically equivalent to a validated RESOLVE_ASPECT Delta and MUST NOT bypass validate_delta/commit_delta.

## 14. v0.6 Observation Frame API Contract

### 14.1 align_observations
Request: context_id, observation_refs (minimum two), clock_mapping_refs, alignment_policy_version, expected_state_version, idempotency_key. Core validates Clock Domain compatibility, mapping validity interval, calibration provenance, time precision, propagated error, and tolerance. Response includes observation_frame_ref, alignment_status, effective_uncertainty_ms, warnings, validation_refs. It MUST NOT assert Entity identity.

### 14.2 revise_observation_frame
A revision request includes observation_frame_ref, new_alignment_evidence_refs, clock_mapping_refs, reason, expected_state_version, idempotency_key. It creates a new Frame with revision_of_ref; the previous Frame remains immutable. Dependent Interpretations/Claims are scheduled for reconciliation.

### 14.3 get_observation_frame
Read-only operation returning Frame, Observation refs, Clock Mapping refs, alignment provenance, uncertainty, revision lineage, and dependent refs at one consistent State Version.

## 15. v0.6 Validator and error additions
validate_delta MUST enforce I-26 through I-35 and detailed design D-21 through D-34. Add error codes: QUESTION_ASPECT_REQUIRED, QUESTION_STATUS_DERIVATION_FAILED, ASPECT_RESOLUTION_BASIS_REQUIRED, ASPECT_DEPENDENCY_UNRESOLVED, ASPECT_LINEAGE_INVALID, CLOCK_MAPPING_REQUIRED, CLOCK_MAPPING_EXPIRED, CLOCK_UNCERTAINTY_EXCEEDED, OBSERVATION_ALIGNMENT_INCOMPARABLE, ENTITY_IDENTITY_NOT_ESTABLISHED, OBSERVATION_FRAME_REVISION_REQUIRED, MIGRATION_REVIEW_REQUIRED.

## 16. v0.6 Migration Contract
Migration is a Core-controlled operation producing a new State Generation. Request includes source/target schema versions, migration policy version, source State Version, idempotency key, and dry-run flag. Response includes migrated refs, unresolved mappings, review items, warnings, and validation report. Migration MUST preserve source records and MUST NOT publish a partially migrated Generation.


---

# v0.8 Compact Recognition Packet Extension

This section extends the existing v0.6-compatible API without changing any prior operation.

## 13. New operations

| Operation | Access | Effect | Purpose |
|---|---|---|---|
| build_context_hint | Core read | None | Return bounded, versioned context references |
| ingest_packet | Write | Append packet/event/evidence references | Accept a bounded source slice |
| submit_classification | Write proposal | No committed state change | Accept compact model output |
| get_packet_status | Read | None | Read packet/continuation/overlap status |
| benchmark_profile | Read/admin | None | Return declared benchmark configuration |

The RecognitionPacket schema is defined in `schemas/surge-v0.8-input.schema.json`.

## 14. build_context_hint

Request: context_id, source_position or source/block refs, requested_scope_refs (optional), max_candidate_refs, expected_state_version (optional), retrieval_policy_version.

Response: state_version, scope_refs, focus_refs, candidate_refs with kind and retrieval_reason, temporal_mode, omitted_candidate_count, truncated, retrieval_policy_version.

The hint is a bounded routing aid, not a complete Context export. Candidate ranking is not truth confidence. The Core MUST preserve unresolved mandatory dependencies and MUST report omissions.

## 15. ingest_packet

Request: context_id, packet conforming to RecognitionPacket, expected_state_version (optional).

Processing:
1. validate schema, digest, offsets, window size, adapter capability and source refs;
2. deduplicate by idempotency key and overlap/source range;
3. append packet receipt and immutable source/Evidence refs;
4. enqueue classification without blocking acceptance;
5. apply backpressure when configured queue capacity is reached.

Response: packet_ref, accepted_source_range, duplicate, classification_job_ref, queue_status (ACCEPTED, BACKPRESSURE, DEFERRED).

No source range may be silently dropped. Overlap does not create duplicate Evidence. Packet receipt is not a semantic commit.

## 16. submit_classification

Request: context_id, packet_ref, producer/model/runtime descriptor refs, base_state_version, labels and candidate spans, abstention reasons, idempotency_key.

Processing:
1. authenticate producer and check capability/label schema;
2. validate all spans against packet offsets and digest;
3. validate producer-local score ranges and calibration declaration;
4. reject stale ContextHint for commit purposes or schedule Core-side revalidation;
5. store immutable proposal;
6. generate a Core-owned Delta candidate for ordinary validation.

Response: proposal_ref, proposal_status (PENDING_VALIDATION, NEEDS_REBASE, ACCEPTED_CANDIDATE, REJECTED), validation_job_ref, abstention_refs.

Classification submission never changes committed Context State directly.

## 17. get_packet_status

Returns accepted source ranges, continuation lineage, overlap group, duplicate status, extraction coverage, classifier status, abstention status and deferred work. It MUST NOT expose a packet as complete when omitted ranges remain.

## 18. Benchmark profile

A benchmark profile includes CPU/runtime identity, model artifact digest/size, quantization, thread count, packet window and overlap, corpus digest, timing boundary, warmup/repetition policy, RTF definition, text reference-rate version, and memory accounting. Benchmark profile is immutable once attached to a result.

## 19. Additional errors

- PACKET_SCHEMA_INVALID
- PACKET_OFFSET_INVALID
- PACKET_WINDOW_EXCEEDED
- PACKET_DIGEST_MISMATCH
- OVERLAP_CONFLICT
- CONTEXT_HINT_STALE
- CLASSIFICATION_LABEL_UNSUPPORTED
- CLASSIFICATION_SPAN_INVALID
- MODEL_DESCRIPTOR_MISSING
- QUEUE_BACKPRESSURE
- BENCHMARK_PROFILE_INCOMPLETE

## 20. Authority

Only Core Transaction Coordinator commits state. Recognition Engine emits candidate labels and spans. Adapter emits structural facts and coverage. Neither can independently confirm Entity identity, source independence, Question resolution, Focus transition or Claim truth.


## 21. v0.8.1 causal-scope and hint-overflow API additions

### 21.1 ingest_packet ordering fields

The packet source-position contract is extended with:
- source_generation
- source_sequence
- source_range_start / source_range_end
- predecessor_packet_refs (optional explicit predecessor set)
- overlap_group_ref (optional)
- source_revision_sequence (when the source supports revisions)

The Adapter assigns sequence values. Core validates monotonicity and immutable assignment within a source generation. Missing sequence ranges are recorded as gaps; Core MUST NOT infer that an absent packet is empty content.

### 21.2 ScopeSpan operations

| Operation | Access | Effect |
|---|---|---|
| propose_scope_span | Proposal write | Immutable candidate only |
| get_scope_span | Read | Read record and continuation lineage |
| resolve_scope_continuation | Core Delta | Close/update via new immutable revision |
| invalidate_scope_span | Core Delta | Invalidate with correction/revision basis |

A proposal includes scope_kind, source_anchor_refs, exact offsets, opener/closer candidates, target candidates, antecedent candidates, packet refs and producer lineage. Proposal submission does not mutate committed Scope State.

Core validation checks offset basis/digest, source ordering, structural compatibility, scope-kind policy, source generation and continuation lineage. A missing closer or antecedent remains OPEN/UNRESOLVED. Packet FINAL alone is not a closer.

### 21.3 RetrievalTicket operations

build_context_hint response is extended with:
- context_complete
- included_ref_digest
- omitted_ref_digest
- omitted_mandatory_count
- omitted_optional_count
- retrieval_ticket_refs

New operations:
- resolve_retrieval_ticket(context_id, ticket_ref, expected_state_version, idempotency_key)
- get_retrieval_ticket(context_id, ticket_ref, at_state_version)

Ticket resolution returns exact candidate refs, retrieval reasons, completeness, resolved_state_version and ticket_status (RESOLVED, STALE_TICKET, EXPIRED, SUPERSEDED). The operation is Context-scoped and cannot retrieve records outside the caller's authorized Context.

### 21.4 Ordered packet integration

get_packet_status adds:
- source_generation
- source_sequence
- integration_status
- committed_watermark
- gap_refs
- blocked_by_packet_refs
- rebase_required
- settled_status

integration_status values: READY, READY_BUT_BLOCKED, NEEDS_REBASE, INTEGRATING, COMMITTED, DEFERRED, REJECTED.

The scheduler may parallelize inference. The Core sequencer controls commit eligibility. A later packet cannot become committed ahead of an unresolved predecessor in the same source generation unless a commutativity proof or authorized gap-resolution record is stored.

### 21.5 New error codes

- SOURCE_SEQUENCE_GAP
- SOURCE_SEQUENCE_REUSED
- SOURCE_GENERATION_MISMATCH
- PACKET_CAUSAL_PREDECESSOR_PENDING
- SCOPE_SPAN_ANCHOR_INVALID
- SCOPE_SPAN_TRANSITION_INVALID
- SCOPE_CONTINUATION_UNRESOLVED
- CONTEXT_HINT_MANDATORY_OVERFLOW
- RETRIEVAL_TICKET_STALE
- RETRIEVAL_TICKET_EXPIRED
- RETRIEVAL_TICKET_SCOPE_VIOLATION
- QUALITY_GATE_NOT_MET

### 21.6 Atomicity and replay

ScopeSpan closure, its supporting Evidence refs, and any Claim polarity/modality update MUST be committed in one Delta. A partial closure is forbidden. RetrievalTicket resolution is read-only; applying its result still requires ordinary proposal validation and commit. Replay preserves original proposal, rebase and commit lineage.
