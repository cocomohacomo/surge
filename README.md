# SURGE

Universal Context Cognition Framework.

## Current specification

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

v0.9 is the current normative architecture and execution contract. v0.8 and earlier specifications remain immutable historical design records. The v0.9 schemas define machine-validatable record shapes; Core API defines operation, authority, transaction, concurrency, replay and error semantics. Cross-record invariants are enforced by the Core Validator.

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
resolution, followed by shared-journal replay. Current CI passes 9/9 CTest
targets and 15/15 Python tests. All 3 Draft 2020-12 schemas and D01-D20 fixture
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
