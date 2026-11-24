# Focus Provenance and Projector Semantic Evaluation v0.1

Status: implementation contract for Core-owned Focus Snapshot provenance and external Projector quality evaluation.

## 1. Observation authenticity

A caller-owned `CognitionObservation` is detached data and must be treated as untrusted at the Snapshot boundary. Matching State Version and matching IDs are insufficient: node content, references, relation kind, endpoints, weights, memberships, and Focus metadata may have been altered while retaining IDs.

The Core-owned Snapshot factory therefore:
1. requires a complete spatial Focus observation;
2. checks the observation State Version against the live ContextGraph;
3. replays the same Focus query against the live graph using the observation's region, positions, depths, and recorded work limits;
4. computes a canonical digest over the full detached observation, not just IDs;
5. rejects any mismatch before producing a Snapshot manifest;
6. rechecks State Version after replay/capture to reject concurrent graph mutation.

Stable errors include `FOCUS_SNAPSHOT_OBSERVATION_MISMATCH`, `FOCUS_SNAPSHOT_LIVE_OBSERVATION_INCOMPLETE`, and `FOCUS_SNAPSHOT_GRAPH_CHANGED_DURING_CAPTURE`.

The canonical digest is versioned and length-delimited. It includes node content and references, relation semantics and weights, memberships, unresolved/question/interest IDs, focus state/history, spatial region, positions, per-cluster depths, logical/processed/deferred roots, and observation completion state. Work timing counters are deliberately excluded.

## 2. Core-owned Focus Provenance

The new overload accepts no digest strings from the caller. It derives:
- Focus Digest from the Focus region, ordered-independent positions/depths, and logical roots;
- Observation Digest from the replay-verified canonical observation;
- Graph Digest from the existing canonical ContextGraph Snapshot Domain.

The older overloads remain for source compatibility, but callers requiring authenticity must use the Core-owned overload. SHA-256 detects accidental or post-capture mutation when compared against a trusted recomputation; it is not a digital signature and does not authenticate a malicious party able to rewrite both data and digest. Signed manifests require a separate key-management contract and are not implied here.

## 3. Projector semantic evaluation

JSON syntax validity is a transport/format property, not semantic quality. The CPU projector benchmark now retains parsed JSON and runs a separate feature evaluation over:
- short acknowledgement / context-dependent response;
- contradiction relation;
- exception and scope relation;
- effective-date / temporal qualifier;
- cross-block reference.

The report separates JSON-valid case count from candidate-marker coverage and expected relation-type coverage. It records per-case hits and a model-level PASS/FAIL feature-coverage gate. A failure is a quality result, not a benchmark execution failure, so model/runtime smoke measurements remain available.

This v0.1 evaluator is intentionally a small challenge-set feature recall gate. It is not a complete semantic precision/recall benchmark, does not judge truth or relevance, and cannot establish general Japanese understanding. The next evaluation expansion should add adjudicated gold candidates, false-positive accounting, evidence-span grounding, confidence calibration, and multi-reference paraphrase matching.

## 4. Core boundary

Projectors remain replaceable external producers. They supply classifications, candidate text, relation labels, and weights. SURGE Core validates structural integrity, preserves provenance, and stores the result; it does not infer semantic truth or silently repair Projector output.
