# SURGE

Universal Context Cognition Framework.

## Current specification

- **Foundational specification v0.4.0:** [docs/surge/SPECIFICATION_v0.4.md](docs/surge/SPECIFICATION_v0.4.md)
- **Detailed design v0.1:** [docs/surge/DETAILED_DESIGN_v0.1.md](docs/surge/DETAILED_DESIGN_v0.1.md)
- **Core API v0.1:** [docs/surge/CORE_API_v0.1.md](docs/surge/CORE_API_v0.1.md)
- **JSON Schema Draft 2020-12:** [schemas/surge-v0.4.schema.json](schemas/surge-v0.4.schema.json)

## Specification hierarchy

The v0.4 foundational specification is the normative architecture and semantic contract. The JSON Schema defines machine-validatable record shapes. The Core API defines operation, authority, transaction, concurrency, and error semantics. Cross-record invariants and lifecycle rules are enforced by the Core Validator.

The v0.3 specification is retained as historical design history. The v0.1 detailed design is retained as the design-decision reference whose normative requirements have been promoted into v0.4.

## Core principles

- Streaming-Native and input-granularity independent
- Model-independent Recognition Engine boundary
- Immutable Evidence and non-destructive context evolution
- Question / Interest / Focus separation
- Relation / Dependency separation with bounded reconciliation
- Contextual Source Trust and Source Lineage
- Core-only commit authority with transactional integrity
- Japanese-first implementation strategy with language-independent core structures
