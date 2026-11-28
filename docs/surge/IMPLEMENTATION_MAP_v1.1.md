# SURGE v1.1 Architecture and Implementation Map

**Normative target:** `SPECIFICATION_v1.1.md`  
**Migration baseline:** v1.0 journal, ContextGraph, Cluster and lifecycle implementation  
**Snapshot:** branch `feat/semantic-gold-corpus-model-gates`, commit before this map is published  
**Rule:** a design document or passing schema check is not proof of a working Core path.

## Architecture target

SURGE v1.1 separates external semantic projection from Core-owned contextual memory. The mandatory semantic input is Information, an optional nearest existing Cluster ID, and a profile-scoped distance. Operational envelope fields support idempotency, ordering, provenance and stale-inventory detection.

## Status by capability

| Capability | v1.1 target | Repository evidence at redesign | Remaining work |
|---|---|---|---|
| Minimal projection contract | Three semantic fields; profile-scoped distance | v1.1 specification, JSON Schema and fixtures added | Expand fixtures and API conformance |
| External Projector boundary | Model/runtime independent | v0.1 minimal adapter exists | Formalize inventory snapshot client and remove Core inference dependencies |
| Cluster Inventory | Read-only, revisioned, complete/paginated | Revision-tagged candidate recall exists | Bind completeness and revision checks to admission |
| Atomic minimal admission | Information + projection + membership in one transaction | Minimal ingress record exists | Connect to ContextGraph membership in same journal transaction |
| No-candidate handling | Provisional singleton or explicit holding Cluster | Contract specified | Implement deterministic IDs, lifecycle and replay |
| Cluster growth | Add membership without semantic attributes | Membership helper exists | End-to-end admission and growth tests |
| Merge / split | Explicit, lineage-preserving lifecycle | Cluster ledger has partial operations | Gate all routes and test atomic graph integration |
| Question / Interest / Focus | Durable context-control, not required model output | Existing components and partial routes | Remove mandatory semantic-recognition coupling; regression tests |
| Journal / replay | Atomic, idempotent, deterministic | Existing journal and packet-semantic transaction substrate | New minimal transaction integration |
| Legacy structured recognition | Optional enrichment / historical compatibility | Existing LLM, Evidence and SemanticDelta paths | Shadow mode, migration and eventual deprecation |
| Semantic quality | Evaluated at Projector boundary | Model matrix and evaluator exist | Independent gold corpus and qualified metrics |
| Resource/performance | Bounded admission and measured edge profile | Journal and ContextGraph benchmarks exist | Sustained full minimal-projection stream on target hardware |

## Required implementation sequence

1. **Admission transaction:** create the Information record, projection observation and membership atomically.
2. **No-candidate lifecycle:** create a provisional singleton or configured holding membership deterministically.
3. **Inventory consistency:** validate exact inventory revision and target lifecycle at commit.
4. **Projector isolation:** ensure Core has no model/runtime dependency; Projector reads a versioned inventory snapshot.
5. **Question / Interest / Focus decoupling:** preserve their state machines while allowing triggers from optional analyzers and structural events.
6. **Replay and failure injection:** test every transaction boundary, duplicate retry, stale inventory and snapshot-tail restore.
7. **Shadow evaluation:** compare legacy and minimal routes without letting the legacy schema define the new Core contract.
8. **Deprecation:** remove mandatory Attribute/Evidence/Relation generation only after the preceding gates pass.

## Release gates

- Schema validation and positive/negative fixture validation.
- C++ and Python admission tests against the same transaction semantics.
- Replay equivalence and idempotency under injected failures.
- Stale target/inventory rejection without partial mutation.
- Question / Interest / Focus regression on minimal-only streams.
- Independent Projector quality evaluation (precision/recall/selective risk).
- Target-device sustained throughput, peak RSS and p50/p95/p99 latency.

No feature in the table is marked implemented solely because the specification or schema exists. Update this map only after checking code, tests and CI for the exact commit.
