# SURGE Projection Interface v0.1

**Status:** Normative design contract  
**Target:** SURGE Specification v0.5 / Core API v0.2  
**Scope:** External information projection into cognition-information clusters  
**Compatibility:** Transport-neutral; maps to `ingest_event`, `submit_proposal`, `validate_delta`, `commit_delta`, and Focus observation

---

## 1. Purpose

The Projection Interface defines how an external Input Adapter or Recognition Engine projects recognized information into SURGE.

It does not define a new cognition principle. It is the boundary between external recognition and the two SURGE fundamentals:

1. cognition-information clusters;
2. Focus as a spatial computation boundary over cognition information.

The same contract MUST accept conversation, documents, structured records, and other modalities after their adapters normalize them. Domain-specific interpretation remains outside Core.

A projection is a proposal, not a committed cognition state. Only Core validation and commit may change durable Context State.

## 2. Authority boundary

| Responsibility | External projector | SURGE Core |
|---|---:|---:|
| Normalize source payload and source position | Yes | Validate |
| Produce semantic candidates and classifications | Yes | Validate structure and references |
| Supply upstream weights and confidence metadata | Yes | Preserve; do not silently recalibrate |
| Propose relations and cluster membership | Yes | Validate and commit |
| Propose spatial placement | Yes | Validate finite coordinates and apply policy |
| Select or resize logical Focus | May request | Authorized Focus controller applies |
| Decide truth, relevance, or action | Application layer | No |
| Maintain immutable evidence and history | No | Yes |
| Enforce versioning, idempotency, invariants, atomic commit | No | Yes |

Core MUST NOT infer that a high confidence value means truth. Confidence values from different producers or calibration scales MUST NOT be directly compared unless a shared calibration contract is explicitly supplied.

## 3. Projection package

A logical package consists of the following records. The package is transport-neutral; implementations may submit them as one proposal or as linked records.

| Field | Required | Meaning |
|---|---:|---|
| `projection_id` | Yes | Unique immutable package identifier |
| `projection_contract_version` | Yes | `0.1.0` |
| `context_id` | Yes | Target Context |
| `base_state_version` | Yes | Snapshot on which recognition was based |
| `producer` | Yes | Producer ID, version, policy version |
| `source_events` | Yes | Input Event references and source ordering |
| `evidence` | Yes | Evidence references back to source Events |
| `interpretations` | Yes | Candidate semantic readings and producer confidence metadata |
| `information_units` | Yes | Semantic units / claims / entities proposed |
| `cluster_memberships` | Yes | Classification and upstream membership weights |
| `relations` | Optional | Explicit links between units, clusters, entities, and evidence |
| `spatial_placements` | Optional | Proposed 3D positions for clusters |
| `focus_request` | Optional | Requested observation region/depth/budget, not an implicit state mutation |
| `idempotency_key` | Yes | Duplicate submission protection |

### 3.1 Package invariants

- Every projected semantic object MUST be traceable to one or more Evidence records.
- Every Evidence record MUST trace to source Event(s), or explicitly identify an external derived source.
- Every membership MUST identify its member, destination cluster, classifier/producer, and upstream weight.
- A weight is transported as supplied. Core MUST NOT normalize it or reinterpret it as truth probability.
- Missing classification MUST NOT be fabricated. The member may remain unresolved or be admitted to a provisional ChaosCluster using the existing open-world path.
- A producer may emit multiple competing interpretations. Ambiguity MUST be retained rather than collapsed to one candidate without an explicit policy.
- Spatial placement is optional metadata. The 3 axes have no universal semantic labels; a projector MUST declare its coordinate-space ID and version.
- Focus requests are read/observation requests unless the caller has explicit permission to mutate persisted Focus configuration.

## 4. Canonical logical envelope

The following is the canonical logical shape. It is an interface-level envelope, not a replacement for the existing v0.5 JSON Schema records.

```json
{
  "projection_contract_version": "0.1.0",
  "projection_id": "proj-0001",
  "context_id": "ctx-demo",
  "base_state_version": 12,
  "idempotency_key": "proj-0001-v1",
  "producer": {
    "producer_id": "recognizer-ja",
    "producer_version": "1.4.0",
    "policy_version": "ja-context-3",
    "coordinate_space_id": "ctx-demo-space",
    "coordinate_space_version": "1"
  },
  "source_events": ["event-0001"],
  "evidence": ["evidence-0001"],
  "interpretations": ["interpretation-0001"],
  "information_units": ["unit-0001"],
  "cluster_memberships": [
    {
      "membership_id": "membership-0001",
      "member_ref": "unit-0001",
      "cluster_ref": "cluster-topic-a",
      "upstream_weight": 0.82,
      "weight_scale": "classifier-membership-v1",
      "producer_ref": "recognizer-ja"
    }
  ],
  "relations": [],
  "spatial_placements": [],
  "focus_request": null
}
```

The arrays contain references to typed records defined by the existing v0.5 schema. The membership envelope adds the producer and scale metadata needed to preserve the meaning of the supplied weight. Implementations MUST map these records to the existing `proposal`, `evidence`, `interpretation`, `semanticUnit`, `relation`, and `stateDelta` contracts rather than introducing parallel committed object types.

## 5. Mapping to existing Core API

| Projection stage | Existing Core API | Durable effect |
|---|---|---|
| Accept raw/normalized input | `ingest_event` | Append immutable Event/Evidence |
| Submit projection package | `submit_proposal` | Store candidate proposal only |
| Check references, permissions, invariants | `validate_delta` | No state mutation |
| Apply accepted subset | `commit_delta` | Atomic Context State update |
| Observe projected information | `get_context` / Focus observation | Read only |

A single source Event may produce multiple Evidence and Interpretation records. A single projection may add several units and relations. The Core may accept a valid subset and return rejected candidate references with reasons. Proposal submission MUST NOT be reported as a successful state commit.

### 5.1 Required validation

Core validation MUST check:

1. package and record schema versions;
2. source Event and Evidence existence;
3. producer identity and producer version;
4. base state version and read-set consistency;
5. unique IDs and idempotency key reuse;
6. membership endpoints and relation endpoints;
7. finite numeric weights and coordinates;
8. coordinate-space identity/version compatibility;
9. allowed lifecycle transitions;
10. provenance completeness and atomic write-set invariants.

A stale base version returns a conflict or rebase requirement. Core MUST NOT silently apply a proposal against an unrelated newer state.

## 6. Projection lifecycle

```text
Source
  -> Adapter normalization
  -> Immutable Event/Evidence registration
  -> External recognition and classification
  -> Projection package
  -> Core proposal validation
  -> Delta validation against base_state_version
  -> Atomic commit
  -> Focus-region observation
  -> External application evaluation/action
```

Recognition failure after Event acceptance MUST NOT delete the accepted Event/Evidence. A rejected proposal MUST remain distinguishable from rejected source evidence. A later corrected projection uses new immutable records and explicit supersession/lineage references.

## 7. Focus and spatial projection

Spatial placement and Focus are separate:

- `spatial_placements` proposes where a cognition cluster is located in the declared cognition space.
- `focus_request` asks to observe a region and per-cluster depths.
- The logical Focus region is determined independently of node/relation processing budgets.
- Budget exhaustion produces a partial observation and deferred work; it MUST NOT contract the logical region.
- Focus depth controls structural expansion from each cluster and is independent of region geometry.
- A projector MUST NOT assume that moving a cluster or changing its membership automatically changes the logical Focus region.

The initial persisted implementation uses an x-axis ordered broad-phase index followed by exact 3D membership checks, and an outgoing-relation adjacency index for local graph exploration. These are derived indexes, not part of the external projection contract or the semantic meaning of the axes.

## 8. Worked projection examples

### 8.1 Conversation

Input: 「来週の会議は火曜日に変更します。資料は月曜日までに共有します。」

External projector proposes:
- two semantic units: meeting schedule change; material-sharing deadline;
- temporal scopes for Tuesday and Monday;
- a relation indicating the material deadline is associated with the meeting;
- source speaker and exact evidence ranges;
- membership in a meeting-planning cluster.

SURGE stores the units and their relation. It does not decide whether the schedule is correct. A later statement that the meeting remains on Wednesday is projected as new evidence and a competing claim, not as destructive replacement.

### 8.2 Document

Input: a policy document with a general rule, an exception paragraph, and a later effective date.

External projector proposes:
- one unit for the general rule;
- one unit for the exception;
- one temporal unit for the effective date;
- `EXCLUDES` or `DEPENDS_ON` relations only where the recognizer has evidence for that relation;
- provenance ranges pointing to the exact paragraphs.

SURGE can expose the related units together when the Focus region covers their clusters. It does not invent an exception relation merely because paragraphs are adjacent.

### 8.3 Structured data

Input: a record `{asset: "A-17", status: "inspection_due", due_date: "2026-12-01"}`.

The adapter creates an Event and Evidence. The projector proposes an asset Entity, a status unit, a date unit, and explicit relations among them. The same cluster membership and relation contract is used as for conversation and documents. No separate structured-data cognition engine is required.

## 9. Simulation protocol and results

This section records deterministic contract-level thought experiments. They are not claims of runtime integration tests or measured performance. Each scenario checks whether the same projection envelope can represent different input types without changing the two SURGE fundamentals.

### Scenario A: conversation, agreement, and later contradiction

Initial projection:
- `unit-meeting-day`: meeting day = Tuesday;
- `unit-material-deadline`: materials due Monday;
- both have Evidence references to the first utterance;
- both are members of `cluster-meeting-plan`.

Second projection:
- a different source states the meeting is Wednesday;
- the projector emits `unit-meeting-day-alt` and a `CONTRADICTS` relation to the Tuesday claim;
- original evidence and claim remain intact.

Expected observation:
- Focus on the meeting-plan region returns both day candidates, their evidence lineage, and the contradiction relation;
- the Core does not choose Tuesday or Wednesday as true;
- the application layer may ask a question or apply a policy.

Result: PASS at contract level. No separate conversation-specific Core structure is required.

### Scenario B: document rule and exception

Projection:
- general rule, exception, and effective date are separate semantic units;
- `EXCLUDES` connects the exception to the exact general-rule scope;
- `DEPENDS_ON` connects applicability to the effective date only if supplied by the recognizer;
- every unit links to paragraph Evidence.

Expected observation:
- Focus spanning the relevant clusters exposes all three units and their relations;
- changing Focus geometry changes the observed region without rewriting cluster membership;
- reducing the node budget may defer some output but cannot shrink the logical region.

Result: PASS at contract level. The exception semantics remain external proposals with traceable evidence.

### Scenario C: structured record update

First projection:
- asset A-17 has status `inspection_due` and due date 2026-12-01.

Second projection:
- a later source changes the due date to 2026-12-05;
- a new unit is emitted with a supersession/temporal relation to the previous unit;
- the first Event/Evidence remains immutable.

Expected observation:
- both old and new values are traceable;
- the current state can identify the newer candidate without erasing the historical value;
- the same Entity/Unit/Relation/Cluster mapping is used as in scenarios A and B.

Result: PASS at contract level. The data adapter differs; the Core projection shape does not.

### Scenario D: ambiguous short utterance

Input: 「うん」

The projector supplies two candidates:
- acknowledgement of the preceding proposal;
- simple backchannel/listener acknowledgement.

Both candidates reference the same Event/Evidence, carry distinct Interpretation IDs and producer confidence metadata, and remain candidates until external policy or further context resolves them.

Expected observation:
- SURGE can preserve both candidates and their context links;
- Core does not force a single interpretation from the short text alone.

Result: PASS at contract level. This verifies that projection granularity need not equal cognition granularity.

### Scenario E: duplicate delivery and stale base version

- Re-submit the same projection with the same idempotency key and identical canonical content.
- Expected: return the original logical result; no duplicate Event/Evidence/commit.
- Re-submit the same key with different content.
- Expected: reject with `IDEMPOTENCY_KEY_REUSED`.
- Submit a proposal based on an obsolete state version.
- Expected: `NEEDS_REBASE` or conflict; no blind commit.

Result: PASS against the existing Core API contract. Runtime behavior still requires API-level tests.

## 10. Simulation summary

| Check | Conversation | Document | Structured | Ambiguous input | Duplicate/stale |
|---|---:|---:|---:|---:|---:|
| Same projection envelope | PASS | PASS | PASS | PASS | PASS |
| Evidence lineage | PASS | PASS | PASS | PASS | PASS |
| Competing candidates retained | PASS | N/A | PASS | PASS | N/A |
| No truth decision in Core | PASS | PASS | PASS | PASS | PASS |
| Focus remains independent | PASS | PASS | PASS | PASS | PASS |
| Runtime integration executed | NOT RUN | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

**Conclusion:** The contract-level simulation supports one common projection interface for conversation, documents, and structured records. It does not establish that all adapters, schema validators, C ABI bindings, or runtime paths are already implemented. The next implementation step is to add a typed ProjectionBatch DTO and a single Core ingestion/validation path that translates the envelope into existing Event, Evidence, Proposal, and Delta records, followed by executable cross-format tests.

## 11. Implementation acceptance criteria

1. One ProjectionBatch accepts all three input families.
2. Every semantic object is traceable to Evidence and source position.
3. Membership weight and confidence metadata are preserved without implicit recalibration.
4. Multiple competing interpretations can coexist.
5. Unknown classification remains unresolved rather than fabricated.
6. Projection submission is separate from commit.
7. Duplicate and stale submissions are deterministic.
8. Spatial placement, Focus geometry, and per-cluster depth remain independent.
9. Focus budget exhaustion never changes logical Focus membership.
10. Conversation, document, and structured-data tests pass through the same Core path.

## 12. Unified source-packet and projection commit

The typed C++ entry point now has two commit modes:

- `apply_projection_batch(ContextGraph&, batch)` for a source packet that has already been admitted.
- `apply_projection_batch(PacketAdmissionCoordinator&, ContextGraph&, batch, packet, source_snapshot)` for atomic source admission and projection commit.

The atomic overload requires the batch context to match the source packet and requires the batch's single `source_event_ref` to equal that packet ID. It prepares the SemanticDelta through `ContextGraph::prepare_events()` and passes the resulting semantic journal events to `PacketAdmissionCoordinator::admit()`. The source packet record and all projection graph mutations are therefore committed by one `EventJournal` transaction. It does not split packet admission and graph mutation into separate commits.

A retry first prepares the same SemanticDelta. If the graph recognizes its committed idempotency fingerprint, it produces no new semantic events; packet admission then resolves the retry as a duplicate. A conflicting packet envelope, stale base state, or reused semantic idempotency key with different content is rejected by the existing admission/graph invariants. If the graph reports an already-committed projection fingerprint but the supplied source packet is not the packet already admitted by the coordinator, the adapter rejects it as `PROJECTION_IDEMPOTENCY_KEY_REUSED` rather than admitting an orphan source packet.

The source event is the admitted RecognitionPacket. The projection's Evidence nodes are committed in the same journal transaction. Source lineage is represented at the ProjectionBatch transaction boundary through `source_event_refs`; Evidence graph nodes retain their existing immutable Core shape and are not given synthetic graph references to packet records. This first atomic adapter accepts one source packet per ProjectionBatch. A document or long stream is represented by successive source packets and batches, not by an artificial multi-source transaction.

## 13. JSON contract and C ABI

`schemas/projection-batch-v0.1.schema.json` defines the transport-neutral JSON shape. The fixture set includes conversation, document, and structured-record examples under `tests/fixtures/projection/`. The schema validation tool checks all three, verifies UTF-8 byte offsets and SHA-256 source digests, checks the source-event reference against the packet ID, and ensures referenced Evidence IDs are represented by Evidence nodes.

The C ABI adds `surge_projection_batch_admit_and_apply()` and the associated `SurgeProjection*Input` structures. It accepts the RecognitionPacket envelope, normalized source bytes, producer/policy metadata, Evidence references, graph nodes, weighted relations, cluster memberships, and spatial placements. It routes these through the same typed `ProjectionBatch` validator and atomic C++ commit function. The ABI version is now 0.5.0. The new `surge_packet_core_resource_snapshot()` query reports current logical resource usage and configured limits without exposing mutable budget controls. The Python `surge_adapter.projection.ProjectionBatchAdapter` validates parsed JSON objects against this schema, verifies NFC UTF-8 source offsets and digests, retains source/Evidence lineage, and marshals the typed data to this C ABI. The schema is installed with the Python package.

The C ABI test covers commit, exact duplicate retry, invalid non-finite membership weight, and replay from the same journal. Struct layouts are included in the ABI layout conformance test.

**Current boundary:** the JSON Schema is the normative JSON contract, but the native library does not yet include a JSON text parser that turns raw JSON directly into `ProjectionBatch`. Applications validate/decode JSON with their chosen parser and pass the typed values through the C ABI or C++ API. The current C ABI's atomic entry point supports one RecognitionPacket per batch. A single ProjectionBatch remains bound to one RecognitionPacket. Ordered multi-packet streams are supported as resumable sequences of per-packet atomic commits; this intentionally does not introduce a cross-packet transaction. A built-in JSON text decoder and format-specific semantic projectors remain separate follow-up work.

## 14. SURGE principle invariants

The projection adapter is intentionally a transport and commit boundary, not a new cognition mechanism:

1. The projector proposes information units, relations, membership weights, and spatial placements. The Core does not select the true interpretation or recalibrate upstream weights.
2. Unknown or competing interpretations remain representable as separate nodes and relations. The adapter does not collapse them into a single answer.
3. Membership is represented by graph relations. Focus is not a target-cluster selector and is not changed implicitly by projection.
4. Spatial placement updates only locate clusters in cognition space. Focus region geometry and each cluster's traversal depth remain independent persisted state.
5. External applications observe the focused cognition region and decide what action, truth or relevance policy to apply.

These constraints keep the two fundamentals unchanged: cognition information clusters and the spatial Focus region operating over them.

## 15. Resource-budgeted projection and packet streams

The production C ABI projection route now uses the handle's `BudgetedContextGraph`, not the graph-only mutation path. Before the packet coordinator commits the atomic source-packet plus semantic-event transaction, the budget facade:

1. estimates projected resident bytes, retained cluster count, and retained semantic events;
2. reserves transient staging capacity;
3. prepares the graph journal events;
4. reserves resident, cluster, and retained-event capacity;
5. stages lease-vector capacity before durable commit.

Only after the packet coordinator reports a successful non-duplicate journal commit does the facade replay the graph from the committed journal and publish the resource leases. Budget rejection therefore occurs before source-packet admission and leaves both packet and graph state unchanged. Duplicate retries do not reserve or publish a second set of persistent leases. The external-commit facade rejects reclamation deltas; projection batches do not use graph reclamation as an implicit side effect.

The C ABI `surge_packet_core_resource_snapshot()` and Python `CppCoreBridge.resource_snapshot` expose read-only used/limit counters for resident bytes, transient bytes, retained events, retained clusters, decision candidates, decision evidence, and combined byte usage. These are logical resource-accounting metrics, not process RSS measurements. The Python ProjectionCommit and ProjectionStreamCommit results additionally report UTF-8 input bytes, processed node/relation/membership/placement counts, and elapsed microseconds measured around adapter validation, marshaling, and the native call. This is end-to-end adapter latency, not an isolated Core CPU-time measurement.

`apply_projection_stream()` and `ProjectionBatchAdapter.commit_stream()` accept ordered sequences of source-packet/batch pairs. Each pair is committed independently using the same atomic packet-plus-projection path. The result reports committed count, duplicate count, next index, current state version, and an error if processing stops. A caller can resume from `next_index`; previously committed items remain safe to replay through idempotency. This is deliberately a sequence of atomic commits, not a synthetic multi-packet transaction, so a later failure does not roll back earlier valid packets.

The stream contract preserves the SURGE boundary: Projectors supply candidate information and weights; Core validates and stores it. No Focus movement, Focus-region resizing, cluster-depth update, truth selection, or relevance decision is implied by stream ingestion.
