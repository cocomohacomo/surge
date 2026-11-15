# SURGE

Universal Context Cognition Framework.

## Current specification

- **Foundational specification v0.9.0:** [docs/surge/SPECIFICATION_v0.9.md](docs/surge/SPECIFICATION_v0.9.md)
- **Detailed design v0.6:** [docs/surge/DETAILED_DESIGN_v0.6.md](docs/surge/DETAILED_DESIGN_v0.6.md)
- **Core API v0.4:** [docs/surge/CORE_API_v0.4.md](docs/surge/CORE_API_v0.4.md)
- **RecognitionPacket Schema Draft 2020-12:** [schemas/surge-v0.9-input.schema.json](schemas/surge-v0.9-input.schema.json)
- **ClassificationProposal Schema Draft 2020-12:** [schemas/surge-v0.9-proposal.schema.json](schemas/surge-v0.9-proposal.schema.json)
- **Benchmark Profile Schema:** [schemas/surge-v0.9-benchmark-profile.schema.json](schemas/surge-v0.9-benchmark-profile.schema.json)
- **D01-D12 fixtures:** [tests/conformance/SURGE_v0.9_D01_D12.json](tests/conformance/SURGE_v0.9_D01_D12.json)
- **D13-D20 fixtures:** [tests/conformance/SURGE_v0.9_D13_D20.json](tests/conformance/SURGE_v0.9_D13_D20.json)

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

D01-D20 are executable-oriented fixtures, not evidence of executed tests. Design review, schema validation, executable conformance, model accuracy and measured performance are reported separately.
