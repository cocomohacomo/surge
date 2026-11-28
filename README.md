# SURGE

Universal Context Cognition Framework.

## Adoption and evaluation\n\n- **v0.9 implementation map:** [docs/surge/IMPLEMENTATION_MAP_v0.1.md](docs/surge/IMPLEMENTATION_MAP_v0.1.md)\n- **Single-use-case evaluation protocol:** [docs/evaluation/MEETING_DECISIONS_EVALUATION_v0.1.md](docs/evaluation/MEETING_DECISIONS_EVALUATION_v0.1.md)\n- **Journal operations, backup and migration:** [docs/surge/JOURNAL_OPERATIONS_v0.1.md](docs/surge/JOURNAL_OPERATIONS_v0.1.md)\n- **Backup/verify/restore tool:** `python tools/journal_backup.py --help`\n- **Common adapter runner:** `python tools/run_meeting_decision_adapter.py --help`\n- **Independent gold corpus staging:** [tests/benchmark/meeting_decisions/README.md](tests/benchmark/meeting_decisions/README.md)\n\n- **Minimal Information Projection v0.1 (migration contract):** [docs/surge/MINIMAL_INFORMATION_PROJECTION_v0.1.md](docs/surge/MINIMAL_INFORMATION_PROJECTION_v0.1.md)\n- **Minimal Projection schema:** [schemas/surge-minimal-projection-v0.1.schema.json](schemas/surge-minimal-projection-v0.1.schema.json)\n- **Minimal Projection fixtures:** [tests/conformance/SURGE_MINIMAL_PROJECTION_v0.1.json](tests/conformance/SURGE_MINIMAL_PROJECTION_v0.1.json)\n\n## Current specification

- **Foundational architecture v1.1.0 (new normative target):** [docs/surge/SPECIFICATION_v1.1.md](docs/surge/SPECIFICATION_v1.1.md)
- **v1.1 minimal projection schema:** [schemas/surge-v1.1-projection.schema.json](schemas/surge-v1.1-projection.schema.json)
- **v1.1 minimal projection conformance fixtures:** [tests/conformance/SURGE_v1.1_MINIMAL_PROJECTION.json](tests/conformance/SURGE_v1.1_MINIMAL_PROJECTION.json)
- **SURGE-owned Cognitive Bias policy:** [docs/surge/COGNITIVE_BIAS_ENGINE_v0.1.md](docs/surge/COGNITIVE_BIAS_ENGINE_v0.1.md)
- **v1.1 implementation and migration map:** [docs/surge/IMPLEMENTATION_MAP_v1.1.md](docs/surge/IMPLEMENTATION_MAP_v1.1.md)
- v1.0 remains the implementation/migration baseline during the additive transition; it is no longer the target architecture. Earlier versions remain historical records.

- **Foundational specification v1.0.0:** [docs/surge/SPECIFICATION_v1.0.md](docs/surge/SPECIFICATION_v1.0.md)
- **Chaos Cluster Model v1.0:** [docs/surge/CHAOS_CLUSTER_MODEL_v1.0.md](docs/surge/CHAOS_CLUSTER_MODEL_v1.0.md)
- **Chaos Cluster Schema:** [schemas/surge-chaos-cluster-v1.0.schema.json](schemas/surge-chaos-cluster-v1.0.schema.json)
- **Chaos Cluster conformance CC01-CC20:** [tests/conformance/SURGE_CHAOS_CLUSTER_v1.0.json](tests/conformance/SURGE_CHAOS_CLUSTER_v1.0.json)
- **Chaos Cluster Core implementation:** [docs/surge/CHAOS_CLUSTER_CORE_IMPLEMENTATION_v1.0.md](docs/surge/CHAOS_CLUSTER_CORE_IMPLEMENTATION_v1.0.md)

- **ContextHint Graph Reasoning v0.2:** [docs/surge/CONTEXT_HINT_GRAPH_REASONING_v0.2.md](docs/surge/CONTEXT_HINT_GRAPH_REASONING_v0.2.md)\n- **Graph-Derived ContextHint v0.1:** [docs/surge/GRAPH_DERIVED_CONTEXT_HINT_v0.1.md](docs/surge/GRAPH_DERIVED_CONTEXT_HINT_v0.1.md)\n- **ContextHint + Durable RetrievalTicket v0.1:** ContextHint priority and journal-backed ticket issue/resolution are integrated into the retrieval bridge. See [CONTEXT_RETRIEVAL_v0.1.md](docs/surge/CONTEXT_RETRIEVAL_v0.1.md).\n- **Persistent Context Retrieval v0.1:** [docs/surge/CONTEXT_RETRIEVAL_v0.1.md](docs/surge/CONTEXT_RETRIEVAL_v0.1.md)\n- **Atomic ScopeSpan Transaction v0.1:** [docs/surge/ATOMIC_SCOPESPAN_TRANSACTION_v0.1.md](docs/surge/ATOMIC_SCOPESPAN_TRANSACTION_v0.1.md)

- **Atomic Packet-Semantic Transaction v0.1:** [docs/surge/ATOMIC_PACKET_SEMANTIC_TRANSACTION_v0.1.md](docs/surge/ATOMIC_PACKET_SEMANTIC_TRANSACTION_v0.1.md)

- **ScopeSpan ContextGraph Integration v0.1:** [docs/surge/SCOPESPAN_CONTEXT_GRAPH_v0.1.md](docs/surge/SCOPESPAN_CONTEXT_GRAPH_v0.1.md)

- **LLM Semantic Core Integration v0.1:** [docs/surge/LLM_SEMANTIC_INTEGRATION_v0.1.md](docs/surge/LLM_SEMANTIC_INTEGRATION_v0.1.md)
- **Streaming Text Runtime v0.1:** [docs/surge/STREAMING_TEXT_RUNTIME_v0.1.md](docs/surge/STREAMING_TEXT_RUNTIME_v0.1.md)

- **ClassificationProposal Builder v0.1:** [docs/surge/PROPOSAL_BUILDER_v0.1.md](docs/surge/PROPOSAL_BUILDER_v0.1.md)

- **Recognition Scoring v0.1:** [docs/surge/RECOGNITION_SCORING_v0.1.md](docs/surge/RECOGNITION_SCORING_v0.1.md)
- **Recognition scoring schema:** [schemas/surge-v0.9-scoring.schema.json](schemas/surge-v0.9-scoring.schema.json)
- **50-case Japanese scoring regression corpus:** [tests/conformance/SURGE_JA_SCORING_50_v0.1.json](tests/conformance/SURGE_JA_SCORING_50_v0.1.json)

- **Foundational specification v0.9.0:** [docs/surge/SPECIFICATION_v0.9.md](docs/surge/SPECIFICATION_v0.9.md)
- **Detailed design v0.6:** [docs/surge/DETAILED_DESIGN_v0.6.md](docs/surge/DETAILED_DESIGN_v0.6.md)
- **Core API v0.4:** [docs/surge/CORE_API_v0.4.md](docs/surge/CORE_API_v0.4.md)
- **Implementation status:** [docs/surge/IMPLEMENTATION_v0.9.md](docs/surge/IMPLEMENTATION_v0.9.md)
- **Runtime execution report:** [docs/surge/RUNNABILITY_REPORT_v0.1.md](docs/surge/RUNNABILITY_REPORT_v0.1.md)
- **RecognitionPacket Adapter v0.1:** [docs/surge/RECOGNITION_PACKET_ADAPTER_v0.1.md](docs/surge/RECOGNITION_PACKET_ADAPTER_v0.1.md)
- **RecognitionPacket Core atomic admission:** [docs/surge/PACKET_ADMISSION_CORE_v0.1.md](docs/surge/PACKET_ADMISSION_CORE_v0.1.md)
- **Packet Admission Core conformance:** [tests/conformance/SURGE_v0.9_PACKET_ADMISSION_CORE.md](tests/conformance/SURGE_v0.9_PACKET_ADMISSION_CORE.md)
- **RecognitionPacket adapter conformance:** [tests/conformance/SURGE_v0.9_RECOGNITION_PACKET_ADAPTER.md](tests/conformance/SURGE_v0.9_RECOGNITION_PACKET_ADAPTER.md)
- **RecognitionPacket Schema Draft 2020-12:** [schemas/surge-v0.9-input.schema.json](schemas/surge-v0.9-input.schema.json)
- **ClassificationProposal Schema Draft 2020-12:** [schemas/surge-v0.9-proposal.schema.json](schemas/surge-v0.9-proposal.schema.json)
- **Benchmark Profile Schema:** [schemas/surge-v0.9-benchmark-profile.schema.json](schemas/surge-v0.9-benchmark-profile.schema.json)
- **D01-D12 fixtures:** [tests/conformance/SURGE_v0.9_D01_D12.json](tests/conformance/SURGE_v0.9_D01_D12.json)
- **D13-D20 fixtures:** [tests/conformance/SURGE_v0.9_D13_D20.json](tests/conformance/SURGE_v0.9_D13_D20.json)

- **ContextHint / RetrievalTicket:** [docs/surge/CONTEXT_HINT_v0.1.md](docs/surge/CONTEXT_HINT_v0.1.md)
- **ScopeSpan conformance matrix:** [tests/conformance/SURGE_v0.9_SCOPE_SPAN.md](tests/conformance/SURGE_v0.9_SCOPE_SPAN.md)
- **ScopeSpan type validator:** [docs/surge/SCOPE_VALIDATOR_v0.1.md](docs/surge/SCOPE_VALIDATOR_v0.1.md)
- **Question / Interest / Focus router:** [docs/surge/QUESTION_ROUTER_v0.1.md](docs/surge/QUESTION_ROUTER_v0.1.md)
- **Dependency impact engine:** [docs/surge/DEPENDENCY_ENGINE_v0.1.md](docs/surge/DEPENDENCY_ENGINE_v0.1.md)
- **Claim temporal validity:** [docs/surge/CLAIM_TEMPORAL_VALIDITY_v0.1.md](docs/surge/CLAIM_TEMPORAL_VALIDITY_v0.1.md)\n- **Japanese temporal recognition:** [docs/surge/JAPANESE_TEMPORAL_RECOGNITION_v0.1.md](docs/surge/JAPANESE_TEMPORAL_RECOGNITION_v0.1.md)
- **Temporal Claim binding:** [docs/surge/TEMPORAL_CLAIM_BINDING_v0.1.md](docs/surge/TEMPORAL_CLAIM_BINDING_v0.1.md)
- **Typed Context Graph:** [docs/surge/CONTEXT_GRAPH_v0.1.md](docs/surge/CONTEXT_GRAPH_v0.1.md)
- **Event journal design:** [docs/surge/EVENT_JOURNAL_v0.1.md](docs/surge/EVENT_JOURNAL_v0.1.md)
- **Event journal conformance:** [tests/conformance/SURGE_v0.9_EVENT_JOURNAL.md](tests/conformance/SURGE_v0.9_EVENT_JOURNAL.md)

## Build and test

C++17 shared Core and C ABI:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python -m pip install -r requirements-dev.txt
python tools/validate_fixtures.py
```

The build produces `build/libsurge_core.so` on Linux (or the platform shared-library equivalent). The Python adapter loads it automatically, or accepts an explicit path through `SURGE_CORE_LIBRARY`. Use `RecognitionPacketAdapter.admit_to_core(packet, source_text, core)` with a `CppCoreBridge(journal_path)` context manager. The adapter validates the Draft 2020-12 schema and NFC byte anchors; the C++ Core independently verifies snapshot/range SHA-256 and performs the authoritative journal transaction.

Schema and fixture checks:

```sh
python -m pip install -r requirements-dev.txt
python tools/validate_fixtures.py  # includes RecognitionPacket adapter tests
```

## Specification hierarchy

- **v1.1 is the normative target architecture:** minimal external Information Projection, Cluster-native admission, model-independent Core, and optional semantic enrichment.
- **v1.0 is the migration/implementation baseline:** its journal, graph, ChaosCluster, lifecycle and context-control components remain readable and reusable, but its structured semantic recognition requirements are not the v1.1 admission contract.
- **v0.9 and earlier are historical compatibility specifications.** Existing records and APIs remain readable during the additive migration.
- See [v1.1 implementation map](docs/surge/IMPLEMENTATION_MAP_v1.1.md) for code-backed status and gates. Schema/fixture validation does not imply full Core conformance.

## Core principles

- Streaming-Native and input-granularity independent
- Model-independent Recognition Engine boundary
- Immutable Evidence and append-only revisions
- Question / Interest / Focus separation
- Typed cross-packet ScopeSpan and causal integration
- Mandatory ContextHint retention with version-bound RetrievalTicket
- Core-only commit authority and audited gap resolution
- Immutable benchmark profiles and semantic/performance dual gates
- Japanese-first implementation strategy with language-independent core structures

## Conformance status

The C++17 Core is built as a shared library and exposes a C ABI for both
journal-backed packet admission and typed SemanticDelta application. The
Python RecognitionPacket and LLM adapters connect through ctypes; C++ performs
independent UTF-8/SHA-256 validation, Core graph validation, and journal-backed
semantic commits. PacketAdmissionCoordinator and ContextGraph share one
EventJournal and recover their respective state after restart.

The provider-neutral LLM interface accepts strict structured recognition
outputs, applies Recognition Scoring, and submits Evidence/Claim/Cluster/
Relation/Question/Interest operations to the C++ Core. CI uses a deterministic
provider fixture and exercises 50 sequential text-to-semantic commits followed
by journal restart replay. A live external LLM endpoint is not called in CI.

The end-to-end runtime test executes packet admission, typed graph commit,
contradiction-to-Question/Interest routing, ambiguous-focus suppression,
dependency impact calculation, ContextHint budget handling and RetrievalTicket
resolution, followed by shared-journal replay. Current CI passes 15/15 CTest targets and 168/168 Python tests. All 5 Draft 2020-12 schemas and D01-D20 fixture
structural contracts validate.

This establishes execution of the implemented LLM-to-Core path, not complete
v0.9 semantic conformance. D01-D20 are not yet all end-to-end semantic tests.
Typed ScopeSpan validation, ContextGraph persistence and restart replay are now implemented. Automatic high-accuracy LLM scope extraction, cross-process cluster and contradiction lookup, cross-process cluster and contradiction lookup, automatic high-quality LLM ScopeSpan extraction, quality metrics, RTF, p99 latency and peak RSS remain unimplemented or unmeasured. See the runtime execution report for the exact
boundary and reproducible commands.

### Packet source integrity

PacketAdmissionCoordinator independently recomputes SHA-256 over the caller-supplied NFC UTF-8 snapshot and its exact half-open byte range. C++ validates UTF-8 but does not normalize Unicode; NFC normalization and JSON Schema validation remain adapter responsibilities. The packet journal now writes RPA2 records and can replay earlier RPA1 records. The Python Adapter now calls the C ABI through ctypes. The end-to-end test covers NFC normalization, durable admission, exact duplicate retry, and restart replay.

## License and reuse

SURGE is licensed under the [MIT License](LICENSE). The MIT License applies
 to project-owned code and documentation; third-party components remain subject
to their respective licenses. See [Publication and reuse policy](docs/PUBLICATION_POLICY.md).


See the [public release readiness checklist](docs/PUBLIC_RELEASE_CHECKLIST.md),
the [audit report](docs/PUBLIC_RELEASE_AUDIT_REPORT.md), and the
[owner protection settings](docs/OWNER_PROTECTION_SETTINGS.md) for
SURGE's completed publication record. To repeat the same process on another
repository, use the reusable
[public release template](docs/public-release-template/README.md) and
`tools/verify_public_release_settings.py`.


## Question relationship network

Question nodes support durable DUPLICATES, CONTAINS, AGREES_WITH, CONFLICTS_WITH, and DEPENDS_ON relations. Question dependency cycles are rejected transactionally. See [Question Relationship Network v0.1](docs/surge/QUESTION_RELATIONSHIP_NETWORK_v0.1.md).


## Question reevaluation discovery

The Core can discover bounded, direct and transitive dependent Questions without changing their lifecycle state. See [Question Reevaluation Discovery API v0.1](docs/surge/QUESTION_REEVALUATION_DISCOVERY_API_v0.1.md).
\n\n## Question forgetting and memory lifecycle\n\nRetention recommendations weigh contradiction strength (0.30), unresolved importance (0.25), Focus relevance (0.20), recurrence (0.15), and recency (0.10). Low-scoring Questions may be marked DORMANT; history and QuestionAspect status remain intact. ARCHIVED is explicit long-term storage, and ACTIVE supports recall. Forgetting never resolves a Question or deletes provenance. See [Question Forgetting and Memory Lifecycle v0.1](docs/surge/QUESTION_FORGETTING_MEMORY_LIFECYCLE_v0.1.md).\n

Capacity enforcement can atomically demote the lowest-scoring active Questions when the configured active-memory limit is exceeded. Scores are required for every currently active Question; ties are resolved by Question ID. See [Question Memory Capacity Enforcement v0.1](docs/surge/QUESTION_MEMORY_CAPACITY_ENFORCEMENT_v0.1.md).
\n\n## Core-derived Question memory planning\n\n`plan_question_memory_capacity(max_active)` is a read-only Core operation that ranks ACTIVE Questions from durable contradiction links, current Focus interest, QuestionAspect lifecycle, aspect-count recurrence proxy, and a neutral recency factor. Questions participating in a Question dependency edge receive a minimum retention score of 0.75. The result includes factor breakdowns and proposed DORMANT candidates; it never changes state. Recency is deliberately neutral because the Core does not infer wall-clock time. Apply a reviewed plan separately with `enforce_question_memory_capacity` and the returned `state_version`.\n

## Compact Recognition Engine

- **Compact local model interface v0.1:** [docs/surge/COMPACT_RECOGNITION_ENGINE_v0.1.md](docs/surge/COMPACT_RECOGNITION_ENGINE_v0.1.md)
- Default model artifact ceiling: 30 MiB; target CPU RTF < 0.01, subject to hardware-specific benchmark.
- Backend-neutral typed candidate interface, SHA-256 artifact identity, measured RTF reporting and deterministic fake-backend tests.
- No model weights or inference runtime are bundled; the interface does not claim the target has been achieved until a real model is benchmarked.


## Recognition Candidate Scoring Bridge

The compact recognition sidecar can be bound to the exact packet/source digest and scored through the pluggable [Candidate Scoring Bridge](docs/surge/RECOGNITION_CANDIDATE_SCORING_BRIDGE_v0.1.md). ACCEPT remains proposal-only; REVIEW and ABSTAIN are preserved, and no score authorizes a Core commit.


## Compact Recognition Pipeline

The explicit orchestration path from local recognition through packet construction and scoring is documented in [Compact Recognition Pipeline v0.1](docs/surge/COMPACT_RECOGNITION_PIPELINE_v0.1.md). Preparation is read-only; packet admission is a separate operation.


## ONNX CPU Backend

An optional [ONNX Runtime CPU backend](docs/surge/COMPACT_ONNX_CPU_BACKEND_v0.1.md) implements the compact model interface with injected tokenizer and candidate decoder. Model weights and tokenizer assets remain separately packaged and license-checked.


## Temporal Reassessment Trace

The read-only [Temporal Reassessment and Evidence Validity Trace](docs/surge/TEMPORAL_REASSESSMENT_EVIDENCE_TRACE_v0.1.md) records Claim validity revisions, Evidence valid-time intervals, provenance timestamps, evaluation instants and explainable decision reasons in a verifiable append-only trace chain.


- **Core Evidence Temporal Validity v0.1:** Evidence valid-time revisions are append-only, transactionally journaled, and queryable at explicit RFC3339 instants. See [EVIDENCE_TEMPORAL_VALIDITY_v0.1.md](docs/surge/EVIDENCE_TEMPORAL_VALIDITY_v0.1.md).


- **Core Temporal Reassessment v0.1:** Core now persists version-pinned Claim/Evidence reassessment snapshots in the EventJournal with SHA-256 hash chaining and replay validation. See [TEMPORAL_REASSESSMENT_CORE_v0.1.md](docs/surge/TEMPORAL_REASSESSMENT_CORE_v0.1.md).


## Cognitive model and security review

- [Chaos Cluster Model v0.1](docs/surge/CHAOS_CLUSTER_MODEL_v0.1.md): preserves unstructured, weakly associated and competing interpretations without forced topic assignment.
- [Whole-Codebase Review v0.1](docs/security/WHOLE_CODEBASE_REVIEW_v0.1.md): static review findings, severity and hardening priorities.


## Replaceable Language Runtime

- **Language Runtime and Replaceable Language Packs v0.1:** [docs/surge/LANGUAGE_RUNTIME_AND_REPLACEABLE_LANGUAGE_PACKS_v0.1.md](docs/surge/LANGUAGE_RUNTIME_AND_REPLACEABLE_LANGUAGE_PACKS_v0.1.md)
- **Language Pack Manifest Schema v1.0:** [schemas/surge-language-pack-v1.0.schema.json](schemas/surge-language-pack-v1.0.schema.json)
- SURGE Core owns language-neutral cognition and decision authority; all language-dependent recognition, models, dictionaries, morphology, n-grams and rendering are supplied by replaceable Language Sets.
- Language Sets are validated and activated by SURGE at session boundaries. Session provenance, resource ceilings, deterministic switching and offline execution are mandatory.
- The specification defines the target contract. Runtime ABI, loader, built-in generic stages and executable conformance remain staged implementation work.


## Install as a C++ library (source build)

The C++ Core can be installed as a relocatable CMake package. This installs
the shared library and public headers and exports the imported target
`surge::surge_core`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /usr/local
```

A downstream CMake project can consume the installation with:

```cmake
find_package(Surge CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE surge::surge_core)
```

The Python adapter can be installed separately with `python -m pip install .`
(or built as a wheel with `python -m build --wheel`). It installs the
versioned schema resources and depends on `jsonschema`. Install the native
C++ library with CMake as above, then set `SURGE_CORE_LIBRARY` to the installed
shared-library path when using the ctypes bridge. The Python wheel does not
bundle a platform-specific native binary yet. See
[the library rollout gate](docs/surge/LIBRARY_ROLLOUT_GATE_v0.1.md) for
supported deployment boundaries and remaining release blockers.
