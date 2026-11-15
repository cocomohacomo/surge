# SURGE

Universal Context Cognition Framework.

## Current specification

- **Foundational specification v0.5.0:** [docs/surge/SPECIFICATION_v0.5.md](docs/surge/SPECIFICATION_v0.5.md)
- **Detailed design v0.2:** [docs/surge/DETAILED_DESIGN_v0.2.md](docs/surge/DETAILED_DESIGN_v0.2.md)
- **Core API v0.2:** [docs/surge/CORE_API_v0.2.md](docs/surge/CORE_API_v0.2.md)
- **JSON Schema Draft 2020-12:** [schemas/surge-v0.5.schema.json](schemas/surge-v0.5.schema.json)

## Specification hierarchy

The v0.5 foundational specification is the normative architecture and semantic contract. The JSON Schema defines machine-validatable record shapes. The Core API defines operation, authority, transaction, concurrency, and error semantics. Cross-record invariants and lifecycle rules are enforced by the Core Validator.

The v0.3 and v0.4 specifications are retained as historical design history. The v0.1 detailed design is retained as the prior design-decision record; v0.2 defines the strengthened v0.5 implementation contracts.

## Core principles

- Streaming-Native and input-granularity independent
- Model-independent Recognition Engine boundary
- Immutable Evidence and non-destructive context evolution
- Question / Interest / Focus separation
- Relation / Dependency separation with bounded reconciliation
- Contextual Source Trust and Source Lineage
- Core-only commit authority with transactional integrity
- Japanese-first implementation strategy with language-independent core structures
