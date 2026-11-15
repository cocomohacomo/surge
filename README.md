# SURGE

Universal Context Cognition Framework.

## Current specification

- **Foundational specification v0.9.0:** [docs/surge/SPECIFICATION_v0.9.md](docs/surge/SPECIFICATION_v0.9.md)
- **Detailed design v0.6:** [docs/surge/DETAILED_DESIGN_v0.6.md](docs/surge/DETAILED_DESIGN_v0.6.md)
- **Core API v0.4:** [docs/surge/CORE_API_v0.4.md](docs/surge/CORE_API_v0.4.md)
- **Implementation status:** [docs/surge/IMPLEMENTATION_v0.9.md](docs/surge/IMPLEMENTATION_v0.9.md)
- **RecognitionPacket Adapter v0.1:** [docs/surge/RECOGNITION_PACKET_ADAPTER_v0.1.md](docs/surge/RECOGNITION_PACKET_ADAPTER_v0.1.md)
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

C++17 Core prototype:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

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

The C++17 prototype currently covers selected v0.9 primitives, an append-only local event journal typed ContextGraph delta commits bounded dependency impact/SCC analysis contradiction-to-Question/Interest/Focus routing and type-specific ScopeSpan validation and bounded ContextHint/RetrievalTicket handling. RecognitionPacket has a Python schema/source-integrity admission adapter with 11 focused conformance tests. D01-D20 are executable-oriented fixtures with structural/range checks; they are not yet all end-to-end semantic tests against the complete Core. Model accuracy, end-to-end RTF, p99 latency and peak RSS remain unmeasured.
