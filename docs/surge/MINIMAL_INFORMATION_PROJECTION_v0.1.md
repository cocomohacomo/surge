# SURGE Minimal Information Projection v0.1

**Status:** Proposed migration contract (non-breaking; does not replace v0.9/v1.0 contracts yet)  
**Intent:** Reduce Core's semantic input to an information payload and an external projection onto the nearest existing context cluster.

## 1. Boundary

SURGE Core is a context-structure and state-management engine, not a semantic extraction model. Semantic interpretation, candidate-cluster retrieval, and distance calculation belong to an external, replaceable Projector.

The Projector may use embeddings, rules, an LLM, a hybrid pipeline, or a domain-specific model. Core must not depend on the Projector's implementation or model.

## 2. Minimal projection

A projection contains only:

- `information`: the incoming information payload, preserved as supplied (string for the initial text profile).
- `target_cluster`: the ID of the nearest existing cluster, or null when the Projector reports no candidate.
- `distance`: the Projector's numeric distance/score for that candidate. Its scale, direction, normalization, and calibration are **not defined by this draft**. They must be declared by a Projector profile; Core stores and compares values only when profiles are compatible.

Example (illustrative, IDs are not canonical):

```json
{
  "information": "柴犬",
  "target_cluster": "cluster-dog",
  "distance": 100
}
```

A candidate-free projection:

```json
{
  "information": "柴犬",
  "target_cluster": null,
  "distance": 0
}
```

The second example's distance value is merely illustrative. Null target means "no candidate was supplied", not a command to create or merge a cluster.

## 3. Operational envelope

Transport/idempotency metadata may wrap the projection (packet/event ID, source, timestamp, sequence, projector/profile version). It is not semantic input and must not become mandatory model-generated content.

Core validates envelope shape, identifier existence, numeric finiteness, profile compatibility, transaction/replay invariants, and cluster lifecycle rules. Core does **not** validate whether the selected cluster is semantically correct.

## 4. Responsibilities

### External Projector
1. Interpret the incoming payload.
2. Search the current cluster inventory or a supplied snapshot.
3. Select a candidate or explicitly report no candidate.
4. Calculate and attach distance using a declared profile.
5. Emit the minimal projection and operational metadata.

### SURGE Core
1. Admit the projection transactionally and preserve the information.
2. Record the projection and its target/distance as provenance.
3. Apply the information to the target cluster's growth/state according to Core rules.
4. Manage cluster lifecycle, relations, lineage, time, Question, Interest, and Focus.
5. Provide cluster inventory/state to Projectors through a read-only interface or versioned snapshot.

Core should not require Attributes, semantic Relations, Phenomena, Evidence Span offsets, quote matching, or model-generated JSON retries to admit a minimal projection.

## 5. Cluster lifecycle safety

- Creating a new cluster and merging existing clusters are separate operations.
- A null target is not itself an automatic merge or a proof of novelty.
- Distance alone must never authorize cluster merge/split.
- Merge, split, and identity decisions remain explicit Core lifecycle operations with lineage and journal history.
- A projection referencing an unknown/stale cluster ID must be rejected or re-evaluated against a current cluster snapshot; silent fallback is prohibited.

## 6. Consistency and stale projection handling

Projection should identify the cluster-inventory revision or digest used for nearest-cluster lookup. Core can detect stale projections. The initial protocol may use a revision field in metadata; it must not add semantic fields to the payload.

## 7. Migration plan (incremental, reversible)

1. **Contract and fixtures:** add the minimal projection schema and deterministic conformance fixtures. No current APIs removed.
2. **Admission path:** add a new Core admission operation that accepts the minimal projection plus envelope; journal it atomically and replay it.
3. **Cluster growth semantics:** implement and test what information growth means without requiring semantic attributes/relations.
4. **External projector adapter:** build a replaceable adapter that emits this contract and binds to cluster snapshot revision.
5. **Parallel comparison:** run minimal projection and legacy structured projector in shadow mode; compare cluster growth, replay integrity, and focus/question/interest changes.
6. **Gate before deprecation:** only deprecate Attribute/Evidence-heavy admission after migration tests and downstream API compatibility checks pass. Legacy contracts remain readable during migration.

## 8. Required tests

- projection with existing target
- candidate-free projection
- unknown target cluster
- stale cluster-inventory revision
- negative, NaN, and infinite distance rejected
- duplicate packet idempotency
- journal commit/replay equivalence
- two projectors emitting the same contract
- cluster growth without generated Attributes/Evidence
- merge is not triggered solely by distance
- Question/Interest/Focus lifecycle regression

## 9. Open decisions

- Distance semantics, scale, and polarity.
- Whether distance is mandatory when target is null, and the neutral representation.
- Text payload versus opaque content reference for non-text streams.
- Whether projection is one candidate or a candidate set in a future version.
- Cluster growth weighting and decay rules.
- Exact cluster-snapshot version/digest protocol.


## 10. Implementation status (2026-11-28)

The repository currently includes a strict external-adapter boundary in
`surge_adapter/minimal_projection.py`. It validates the three-field contract,
rejects non-finite distance values, preserves the information string and does
not interpret distance polarity or trigger cluster lifecycle operations.
Unit coverage is provided by `tests/test_minimal_projection.py`, and the
fixture validator checks this contract's schema and positive fixtures.

This is **not yet a Core admission implementation**. The existing
`ProjectionBatch` path still requires source-event and Evidence references
and emits a structured `SemanticDelta`. A separate journal-backed minimal
admission command, durable distance/projection provenance, replay support,
and cluster-growth behavior remain pending. Do not describe the Core as
minimal-projection capable until those operations and restart tests pass.
