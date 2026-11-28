# SURGE 基礎仕様 v1.1

## Streaming Information Projection and Cluster-Native Context Core

- Version: 1.1.1
- Clarification: SURGE-owned Cognitive Bias classification and weighting
- Status: Normative architecture redesign
- Supersedes: `SPECIFICATION_v1.0.md` as the target architecture
- Migration baseline: v1.0 implementation and journal contracts remain readable
- Primary principle: **Project minimum information; let SURGE's cognition classify, weight, and evolve context inside.**

---

## 1. Purpose and design correction

SURGE is a model-independent, streaming-native framework for retaining information and evolving its contextual structure over time.

SURGE Core is **not** a language model, evidence-span extractor, or truth classifier. A replaceable external Projector performs only the minimum external projection: it supplies incoming Information, the nearest existing Cluster (if any), and the associated distance/score. It MUST NOT decide SURGE's final classification or membership weight.

SURGE owns an internal, versioned **Cognitive Bias** policy. This policy evaluates the minimal projection against SURGE-held cognition state, including association affinity, current Focus, Interest, unresolved Question pressure, novelty, and recurrence. SURGE itself determines the cognitive classification and membership weight, then persists the decision trace and graph changes. The bias policy is an explicit part of the framework's cognition, not an external Projector output.

The framework must support a tiny acknowledgement, a word, a sentence, a document span, or a long-running stream without assuming that input length determines importance.

### 1.1 Non-goals of the Core

The Core MUST NOT require a model to generate:
- Attribute or predicate inventories;
- Claims or normalized propositions;
- semantic Relation types;
- Phenomena labels;
- Evidence Span offsets or quote matching;
- confidence vectors or model-specific scoring dimensions;
- a fully populated JSON semantic graph.

These may exist as optional, separately versioned enrichment products. They are not admission prerequisites and cannot become hidden mandatory fields of the minimal contract.

### 1.2 Normative vocabulary

- **MUST / MUST NOT**: mandatory conformance requirement.
- **SHOULD / SHOULD NOT**: recommended unless a documented reason exists.
- **MAY**: optional extension.
- **Information**: immutable incoming content or a reference to immutable content.
- **Projection**: an external association proposal connecting Information to at most one nearest existing Cluster in the initial profile.
- **Cluster**: durable contextual memory container.
- **Membership**: a durable association between an Information node and a Cluster.
- **Projector**: replaceable external component that chooses the nearest candidate and calculates a declared distance/score.
- **Inventory revision**: identity of the Cluster inventory snapshot used by the Projector.
- **Admission**: atomic Core operation that makes information durable and cluster-resident.

---

## 2. Fundamental invariants

1. **Information preservation.** Admitted information is immutable. Corrections are new events with explicit supersession/lineage; they never rewrite the original payload.
2. **Cluster-native memory.** Every admitted information item MUST have at least one durable Cluster membership in the same atomic transaction.
3. **Projection is not truth.** Membership means contextual placement only. It does not assert equivalence, truth, causality, agreement, or identity.
4. **Core cognitive autonomy.** The Core does not accept external classification or membership weight as authoritative. It applies its own versioned Cognitive Bias policy to the minimal projection and current internal state. The Projector's target and distance are provenance inputs, not final cognition decisions.
5. **Replaceable Projector.** The Core contract is independent of the model, language, vector dimension, tokenizer, prompt, and inference runtime.
6. **Append-only evolution.** Changes to membership, cluster lifecycle, lineage, and projection are recorded as new durable events.
7. **No distance-only merge.** A distance value MUST NOT independently authorize cluster merge, split, deletion, or identity resolution.
8. **No silent loss.** Capacity rejection, stale projection, invalid reference, or journal failure is explicit and leaves the transaction unapplied.
9. **Deterministic recovery.** Journal replay reconstructs the same IDs, memberships, lifecycle and state version.
10. **Attention is not truth.** Question, Interest, and Focus affect what is examined, not whether information is true or whether its history may be erased.

---

## 3. Architecture

### 3.1 Logical components

| Component | Responsibility | Must not do |
|---|---|---|
| Input Adapter | Convert source formats into immutable Information plus transport metadata | Build a parallel context graph |
| External Projector | Provide Information, nearest candidate Cluster, distance/score and metric profile | Decide SURGE classification or membership weight; commit Core state |
| Cluster Inventory API | Return read-only Cluster/member summaries and a revision token | Expose mutable internal storage |
| Admission Gateway | Validate envelope, revision, target existence, idempotency and resource budget | Validate semantic correctness |
| SURGE Core | Apply Cognitive Bias, determine classification and membership weight, persist Information, decision trace, membership, lifecycle and context state | Depend on external semantic labels/weights or require model-generated extraction |
| Event Journal | Atomic append, idempotency and deterministic replay | Treat uncommitted frames as state |
| Optional Enrichment | Add Claims, typed Relations, Evidence references, temporal interpretation or other domain data | Block minimal admission |
| Observer / Retrieval | Read graph views under explicit budget and completeness metadata | Mutate state through a read API |

### 3.2 Data flow

1. Adapter captures Information and stable source/ordering metadata.
2. Core exposes a read-only Cluster Inventory snapshot and its revision.
3. Projector interprets Information against that exact snapshot.
4. Projector returns one nearest Cluster and a distance under a declared metric profile, or explicitly returns no candidate.
5. Admission Gateway checks structural validity and that the inventory revision and target are still current.
6. Core applies its internal Cognitive Bias policy using the projection and current cognition state, determines classification and membership weight, then atomically stores Information, projection provenance, bias decision trace and membership. If no candidate was supplied, Core uses its unresolved/new-information bias path and creates a provisional singleton Cluster or configured holding ChaosCluster.
7. Later projections may associate further Information with that Cluster. Any merge, split or re-parenting is a separate lifecycle operation with lineage.

The Projector can be replaced without changing the Core. A Projector is free to use a different model, provided it emits the same contract and declares a compatible metric profile.

---

## 4. Minimal Information Projection contract

### 4.1 Semantic payload

The initial text profile has exactly three semantic fields:

| Field | Type | Meaning |
|---|---|---|
| `information` | string | Incoming content, preserved exactly after the adapter's declared normalization policy |
| `target_cluster` | string or null | Nearest existing Cluster ID, or null when no candidate is supplied |
| `distance` | finite number or null | Distance/score for that target under the declared metric profile |

Example:

```json
{
  "information": "柴犬",
  "target_cluster": "cluster-dog",
  "distance": 100
}
```

No-candidate example:

```json
{
  "information": "柴犬",
  "target_cluster": null,
  "distance": null
}
```

A null target MUST have a null distance. A non-null target MUST have a finite numeric distance. Zero is a valid numeric result, not a universal representation of "no relation".

### 4.2 Distance is profile-scoped

The Core MUST NOT assume that a larger or smaller value means closer. A Projector profile MUST declare:
- `metric_id`: stable metric identifier;
- `metric_version`: metric implementation/version;
- `direction`: `distance_lower_is_closer` or `similarity_higher_is_closer`;
- `scale`: declared numeric range or `unbounded`;
- `normalization_id`: normalization/calibration identity, or `none`;
- `projector_id` and `projector_version`.

The Core stores the numeric value and profile identity. It may normalize the value only within the declared metric profile to derive association affinity for its own Cognitive Bias policy. Values from incompatible profiles MUST NOT be compared, averaged, or ranked against one another. If the Projector uses a similarity score, the field name remains `distance` for wire compatibility only; the profile direction defines its meaning. A later protocol version may rename this field to `score`.

The Projector is responsible for applying its own candidate threshold. If no candidate meets its threshold, it returns null target and null distance. Core MUST NOT invent a universal threshold.

### 4.3 Operational envelope

Operational metadata is separate from the three semantic fields. The envelope SHOULD include:
- `schema_version`;
- `packet_id` and `idempotency_key`;
- `source_id`, source sequence or source position;
- `observed_at`;
- `projector_profile`;
- `inventory_revision`;
- optional immutable payload digest and content reference.

These values support ordering, audit, replay and stale-snapshot detection. They are not semantic output and MUST NOT be generated by the model.

For non-text inputs, a future profile MAY allow `information` to be an immutable content reference with media type and digest. The initial v1.1 text profile uses a string.

---

## 5. Cluster lifecycle and growth

### 5.1 SURGE-owned Cognitive Bias decision

The minimal projection is deliberately information-poor. SURGE uses its own current state and a versioned policy to classify and weight it. The initial deterministic policy exposes these bias dimensions:

- Association affinity: derived from the supplied distance and its declared metric profile.
- Focus alignment: relationship to the current logical Focus.
- Interest activation: existing Interest allocated to the target or related context.
- Question pressure: unresolved Questions associated with the target context.
- Novelty: how little the incoming information is already represented in the current context.
- Recurrence: repeated contextual activation, with safeguards against repetition being mistaken for importance.

The policy produces a cognitive classification (the dominant bias axis), a membership weight in [0,1], and a trace containing policy identity, normalized affinity, evaluated internal signals, and per-axis contributions. These classifications describe SURGE's way of attending to and organizing information; they are not semantic labels such as DOG, PERSON, or LOCATION, and they are not truth/confidence judgments.

The default policy is a deterministic baseline, not a universal cognitive law. It MUST be versioned and calibrated against independent cases. Missing internal signals are UNKNOWN and MUST NOT silently be converted to strong positive evidence. Policy evaluation and graph mutation MUST be deterministic for a pinned state version.

### 5.2 Admission with an existing target

When `target_cluster` identifies a current Cluster, Core applies the Cognitive Bias decision and atomically:
1. creates the immutable Information node or immutable source reference;
2. records the projection observation, including target, distance and metric profile;
3. creates the membership edge from the target Cluster to the Information;
4. advances the journal/state version once.

The membership records SURGE's Cognitive Bias weight. The raw Projector distance, metric profile, policy version, evaluated signals and contribution trace remain recoverable. The external Projector does not supply the final Contains-edge weight.

### 5.3 Admission without a target

When target and distance are null, Core MUST still retain the Information. The default policy is to create a provisional singleton Cluster and membership atomically. The new Cluster receives a deterministic or replay-stable ID and a lifecycle marker indicating provisional/unclassified placement. This is structural isolation, not a semantic label.

An implementation MAY instead place the Information in a configured holding ChaosCluster. The policy MUST be explicit and deterministic. It MUST NOT silently discard or transiently buffer the item without durable membership.

### 5.4 Growth is not automatic fusion

A later Information item projected to an existing Cluster grows that Cluster by adding a member. This does not require merging two pre-existing Clusters.

If two existing Clusters should be combined, Core requires an explicit merge operation with:
- source and destination Cluster IDs;
- expected graph/inventory revision;
- reason and provenance reference;
- preserved member identities;
- lineage from both old Clusters;
- one atomic journal transaction.

Split and re-parent operations follow the same explicit, append-only lifecycle principle. Distance alone never triggers them.

### 5.5 Multi-membership

The v1.1 base projection selects at most one target Cluster. The Core graph MAY support a node belonging to multiple Clusters, but multi-target Projector output is a future protocol extension. Multiple memberships added by later explicit Core operations must reference the same canonical Information node; payload duplication is prohibited.

### 5.6 Cluster types

- **Provisional Cluster:** newly created structural container for information with no supplied target.
- **Context Cluster:** a cluster receiving subsequent contextual membership.
- **ChaosCluster:** open-world container for plural, unresolved, exploratory or weakly organized information.

ChaosCluster is not a garbage bin, low-importance tier, or automatic semantic classifier. A Provisional Cluster MAY later be associated with a Context Cluster or ChaosCluster through an explicit operation while preserving lineage.

---

## 6. Context graph and optional semantic enrichment

The canonical durable substrate remains a typed, many-to-many Cluster Graph. The minimum admission path requires only Information, projection observation, Cluster and membership.

Optional typed records MAY include:
- Claim / proposition;
- Entity / reference;
- Evidence / source lineage;
- Relation / Dependency;
- temporal validity;
- Question / QuestionAspect;
- Interest;
- PatternHypothesis and ChaosCluster association.

These records are independent enrichment layers. They MUST be append-only or revisioned, provenance-bound, and replayable. They MUST NOT be required to admit Information or to make the initial membership durable.

Semantic Relation types such as SUPPORTS, CONTRADICTS, AGREES_WITH, DEPENDS_ON, or CAUSED_BY are not inferred by the base Core from raw text or distance. They may be proposed by an optional external analyzer or by an explicit higher-level application. Core validates IDs, types, lifecycle, and transaction integrity, then persists accepted proposals.

Evidence is retained when supplied, but Evidence Span offsets, quote extraction and model-produced start/end indices are not mandatory in the minimal path. Source integrity metadata (digest, immutable source reference, observed time) remains useful and distinct from semantic Evidence extraction.

---

## 7. Question, Interest and Focus

These are context-control mechanisms, not semantic parsing requirements.

### 7.1 Question

Question represents an unresolved aspect, contradiction candidate, ambiguity, missing dependency, or review request. The Core owns Question identity, lifecycle, relations, recurrence and persistence. A trigger may be proposed by an external analyzer, an application, or a Core-detectable structural invariant. The Core MUST NOT claim to discover natural-language contradiction without a semantic producer.

Question lifecycle is append-only and may include OPEN, INVESTIGATING, DEFERRED, RESOLVED and SUPERSEDED. Resolution requires a basis reference. New information may reopen or supersede a prior Question without rewriting history.

### 7.2 Interest

Interest represents allocation priority for a target Cluster, Question, or bounded context. It may consider unresolvedness, impact, dependency reach, recurrence, recency, explicit attention and current context relevance. Each factor retains its scale, source and evaluation version. Missing values are UNKNOWN, not zero.

Interest is not truth confidence, semantic similarity, or cluster membership weight. It MUST NOT be inferred from the Projector distance alone.

### 7.3 Focus

Focus defines which parts of the graph are currently selected for bounded observation or processing. It may be single or multiple, scoped, nested, and budgeted. Focus changes do not mutate membership, truth, Cluster lifecycle, or Information.

The logical Focus model is normative; spatial coordinates, 3D geometry, axis-aligned boxes and spatial indexes are optional implementation strategies only. A budget-limited observation MUST distinguish in-scope, processed and deferred items. It MUST NOT shrink the logical Focus region merely because a processing budget was exhausted.

Question can raise Interest; Interest can influence Focus. This is a control flow, not a claim that a Question has been semantically resolved or that a focused item is true.

---

## 8. Streaming, ordering and granularity

- Every source event has stable identity and source-local ordering metadata.
- The Core preserves source order within a source stream. It does not invent a total order across independent sources.
- Incomplete input MAY be admitted as provisional revisions. Finalization is a new event/state transition, not destructive replacement.
- Packet identity and idempotency are independent from semantic similarity.
- Duplicate packet with identical content and idempotency key returns the original result; changed content under the same key is rejected.
- Chunking differences may produce different intermediate states. The target property is convergence to a contextually equivalent final state when normalized information, order and finalization boundaries are equivalent.
- A short input such as "うん" is admitted without assuming it is unimportant. Any interpretation such as agreement or acknowledgement belongs to the Projector/enrichment layer and may be revised later.

---

## 9. Inventory API and stale projection control

Projector retrieval MUST be read-only and versioned. A snapshot response includes:
- inventory revision or digest;
- stable Cluster IDs and lifecycle states;
- bounded summaries or references sufficient for candidate selection;
- completeness metadata and continuation cursor when paginated;
- projector-visible policy/profile identity if applicable.

A projection references the revision it used. If the inventory has changed in a way that invalidates the target or candidate search basis, Core rejects admission with a typed stale-projection result and no mutation. The caller refreshes the inventory and reprojects. Implementations MAY permit a target-stable fast path only if the exact consistency rule is documented and tested.

A retrieval ranking score is not automatically the projection distance. Candidate retrieval and final nearest-candidate selection remain distinct Projector responsibilities.

---

## 10. Transaction, journal and replay contract

Information admission, projection observation and membership creation MUST be one atomic transaction.

The transaction includes:
- packet/source identity and idempotency record;
- immutable Information payload/reference and digest where available;
- projector profile and inventory revision;
- target Cluster or newly created provisional Cluster;
- membership edge;
- state version transition.

The Core validates before commit. No in-memory published state changes before the journal commit marker succeeds. Failure before commit leaves graph version, journal watermark, membership, packet index and idempotency index unchanged. Replay reconstructs exactly the same Information ID, Cluster ID, membership and state version.

Existing EventJournal framing, atomic append, snapshot verification, backup and replay machinery SHOULD be reused. The new contract must not create a second independent source of truth.

---

## 11. Resource control and bounded operation

Resource admission is explicit. The Core reserves bounded memory and journal capacity before materializing large inputs or mutation batches. On exhaustion it returns backpressure or a retryable capacity result; it never silently loses an event.

Logical reservations, allocator usage and process RSS are separate. A configured logical budget is not a hard RSS guarantee. Production claims require sustained target-device measurements for peak RSS, transient high-water, queue depth, throughput and p50/p95/p99 latency.

Cluster Inventory pagination, graph observation and replay MUST report completeness. A bounded page is not the complete inventory and MUST NOT be passed to a Projector as complete without an explicit completeness marker.

---

## 12. Public API shape

The v1.1 conceptual API is:

```text
get_cluster_inventory(query, expected_revision?) -> InventorySnapshot

admit_projection(
    information,
    target_cluster,
    distance,
    projector_profile,
    inventory_revision,
    packet_envelope
) -> AdmissionResult

append_cluster_merge(source_ids, destination_id, expected_revision, provenance)
append_cluster_split(cluster_id, partition, expected_revision, provenance)
observe_context(focus, budget) -> ContextObservation
```

The first operation is read-only. `admit_projection` is the only required write path for minimal information. Merge/split are explicit lifecycle APIs. The exact C++, C ABI and Python names are implementation decisions; their semantics MUST match this contract.

Admission results include committed state version and canonical IDs, or a typed error such as INVALID_ENVELOPE, UNKNOWN_TARGET, STALE_INVENTORY, IDEMPOTENCY_CONFLICT, RESOURCE_EXHAUSTED, JOURNAL_FAILURE. A failed result MUST NOT imply partial admission.

---

## 13. Conformance requirements

A v1.1 conforming implementation MUST test:

### Contract and projector boundary
1. existing target admission;
2. no-candidate admission creates a durable provisional Cluster or configured holding membership;
3. two different Projectors can emit the same Core contract;
4. distance profile direction and version are retained without Core reinterpretation;
5. incompatible metric profiles are never compared by Core;
6. null target requires null distance;
7. NaN, infinity and malformed numeric values are rejected;
8. unknown target and stale inventory revision fail without mutation.

### Atomicity and recovery
9. packet idempotent retry;
10. changed payload under same key rejected;
11. journal failure at every transaction frame leaves state unchanged;
12. restart replay recreates identical graph and IDs;
13. snapshot restore plus journal tail yields the same state;
14. concurrent/stale admission cannot commit against an obsolete revision.

### Cluster lifecycle
15. adding information grows a target Cluster without requiring Attributes or Evidence;
16. no-target information is retained as a provisional singleton;
17. merge/split are explicit and lineage-preserving;
18. distance alone never merges or deletes clusters;
19. shared member is not duplicated across memberships;
20. archived/dormant/protected clusters obey lifecycle and retention constraints.

### Context control and streaming
21. Question / Interest / Focus lifecycle works without being mandatory model output;
22. Focus budget reports deferred items without changing logical scope;
23. short acknowledgements are durably retained;
24. chunked and unchunked equivalent streams converge to equivalent final state;
25. sustained streaming applies backpressure without silent loss.

### Quality and performance
26. semantic quality is measured on independent, adjudicated corpora at the Projector boundary;
27. Core correctness is measured separately through deterministic conformance and replay;
28. RTF, p50/p95/p99, peak RSS, queue depth and memory growth are measured on declared target hardware.

Passing schema validation alone does not establish Core conformance. Passing Core tests does not establish Projector semantic accuracy. A CI pass does not establish target-device performance.

---

## 14. Migration from v1.0

The migration is additive and reversible.

### Stage A: establish v1.1 contract
- Publish the v1.1 specification, schema and fixtures.
- Keep all v1.0 records readable.
- Mark v1.0 structured Recognition as a legacy/enrichment producer, not the mandatory Core input.

### Stage B: independent minimal admission
- Implement the minimal Admission Gateway using the existing EventJournal and ContextGraph transaction boundary.
- Atomically create Information, projection observation and membership.
- Implement provisional singleton/holding-cluster behavior.
- Add stale inventory and idempotency handling.

### Stage C: projector isolation
- Move semantic candidate generation and distance calculation behind the external Projector interface.
- Expose a read-only, versioned Cluster Inventory.
- Ensure Core does not load a model or call an inference runtime.

### Stage D: parity and shadow evaluation
- Run legacy structured output and minimal projection in shadow mode.
- Compare retention, membership, replay, Question/Interest/Focus behavior and resource use.
- Do not compare semantic output as if the Core were its judge; evaluate Projector accuracy independently.

### Stage E: retire mandatory structured admission
- Only after v1.1 conformance and downstream migration pass, remove Evidence/Attribute/Relation requirements from the default admission path.
- Keep optional enrichment APIs and historical journal readers where useful.
- Do not rewrite old journal records or historical specification files.

### Compatibility rule
v0.x and v1.0 specifications remain historical design records. Existing journal records and APIs remain readable during migration. The README and implementation map must clearly distinguish normative target, implemented subset, legacy compatibility and unimplemented work.

---

## 15. Explicitly deferred decisions

The following are intentionally not fixed by the base specification:
- numeric distance scale, polarity and calibration values;
- universal candidate threshold;
- automatic semantic cluster naming;
- semantic merge/split heuristics;
- model architecture, embedding dimension, tokenizer or prompt;
- multi-target projection protocol;
- multimodal payload schema;
- automatic natural-language contradiction discovery;
- a mandatory 3D spatial Focus representation;
- a universal cluster-growth decay or importance formula.

These belong to Projector profiles, optional policy modules, or later protocol revisions. They MUST NOT leak into the Core's minimal contract.

---

## 16. Implementation status at publication

This specification defines the target architecture; it does not claim that the target is already implemented.

The repository already has portions of the required substrate, including a journal-backed minimal projection ingress record, a read-only revision-tagged candidate recall API, typed Cluster membership helpers, append-only Journal/Replay, ChaosCluster structures and Question/Interest/Focus components.

The current minimal projection ingress is not yet a complete atomic graph admission path: it must still connect Information creation, target validation, membership creation and inventory revision validation in one transaction. Existing legacy structured LLM paths remain present. The v1.1 conformance suite and all public ingress routing are future implementation gates.

Implementation status MUST be updated from code and CI evidence, not inferred from this document.
