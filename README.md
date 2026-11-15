# SURGE

Universal Context Cognition Framework.

## Current specification

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

The C++17 Core is built as a shared library and exposes a stable C ABI. The
Python RecognitionPacket adapter connects through ctypes; C++ performs
independent UTF-8/SHA-256 validation and journal-backed atomic packet admission.
PacketAdmissionCoordinator and ContextGraph can share one EventJournal and
recover their respective state after restart.

The end-to-end runtime test executes packet admission, typed graph commit,
contradiction-to-Question/Interest routing, ambiguous-focus suppression,
dependency impact calculation, ContextHint budget handling and RetrievalTicket
resolution, followed by shared-journal replay. Current CI passes 9/9 CTest
targets and 15/15 Python tests. All 3 Draft 2020-12 schemas and D01-D20 fixture
structural contracts validate.

This establishes execution of the implemented Core path, not complete v0.9
semantic conformance. D01-D20 are not yet all end-to-end semantic tests.
Automatic text-to-Claim/Evidence/ScopeSpan recognition, actual Recognition
Engine/model integration, quality metrics, RTF, p99 latency and peak RSS remain
unimplemented or unmeasured. See the runtime execution report for the exact
boundary and reproducible commands.

### Packet source integrity

PacketAdmissionCoordinator independently recomputes SHA-256 over the caller-supplied NFC UTF-8 snapshot and its exact half-open byte range. C++ validates UTF-8 but does not normalize Unicode; NFC normalization and JSON Schema validation remain adapter responsibilities. The packet journal now writes RPA2 records and can replay earlier RPA1 records. The Python Adapter now calls the C ABI through ctypes. The end-to-end test covers NFC normalization, durable admission, exact duplicate retry, and restart replay.
