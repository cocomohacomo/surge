# SURGE v1.1 Architecture and Implementation Map

**Normative target:** `SPECIFICATION_v1.1.md`  
**Migration baseline:** v1.0 journal, ContextGraph, Cluster and lifecycle implementation  
**Snapshot:** branch `feat/semantic-gold-corpus-model-gates`, commit before this map is published  
**Rule:** a design document or passing schema check is not proof of a working Core path.

## Architecture target

SURGE v1.1.1 separates minimum external projection from Core-owned cognition. The Projector supplies Information, an optional nearest Cluster ID and profile-scoped distance. SURGE's internal Cognitive Bias policy, not the Projector, determines cognitive classification and membership weight from Core-held Focus, Interest, Question, novelty and recurrence signals. Operational envelope fields support idempotency, ordering, provenance and stale-inventory detection.

## Status by capability

| Capability | v1.1 target | Repository evidence at redesign | Remaining work |
|---|---|---|---|
| Minimal projection contract | Information + nearest Cluster + profile-scoped distance | v1.1 specification, JSON Schema and fixtures added | Expand fixtures and API conformance |
| External Projector boundary | Model/runtime independent | v0.1 minimal adapter exists | Formalize inventory snapshot client and remove Core inference dependencies |
| Cluster Inventory | Read-only, revisioned, complete/paginated | Revision-tagged candidate recall exists | Bind completeness and revision checks to admission |
| Cognitive Bias evaluator | Core-owned classification and weight policy | C++ evaluator, policy trace and CMake conformance target added | Derive signals from graph state; UNKNOWN semantics; calibration |
| Atomic minimal admission | Information + projection + bias decision + membership in one transaction | Minimal ingress record exists; bias evaluator is standalone | Connect evaluator, Information node and ContextGraph membership in same journal transaction |
| No-candidate handling | Provisional singleton or explicit holding Cluster | Contract specified | Implement deterministic IDs, lifecycle and replay |
| Cluster growth | Add membership without semantic attributes | Membership helper exists | End-to-end admission and growth tests |
| Merge / split | Explicit, lineage-preserving lifecycle | Cluster ledger has partial operations | Gate all routes and test atomic graph integration |
| Question / Interest / Focus | Durable context-control, not required model output | Existing components and partial routes | Remove mandatory semantic-recognition coupling; regression tests |
| Journal / replay | Atomic, idempotent, deterministic | Existing journal and packet-semantic transaction substrate | New minimal transaction integration |
| Legacy structured recognition | Optional enrichment / historical compatibility | Existing LLM, Evidence and SemanticDelta paths | Shadow mode, migration and eventual deprecation |
| Semantic quality | Evaluated at Projector boundary | Model matrix and evaluator exist | Independent gold corpus and qualified metrics |
| Resource/performance | Bounded admission and measured edge profile | Journal and ContextGraph benchmarks exist | Sustained full minimal-projection stream on target hardware |

## Required implementation sequence

1. **Core signal derivation:** derive Focus, Interest, Question pressure, novelty and recurrence signals from the pinned ContextGraph state; define UNKNOWN behavior.
2. **Bias decision trace:** persist policy identity, per-axis inputs/contributions, cognitive classification and membership weight.
3. **Admission transaction:** create the Information record, projection observation, bias trace and membership atomically.
4. **No-candidate lifecycle:** create a provisional singleton or configured holding membership deterministically.
5. **Inventory consistency:** validate exact inventory revision and target lifecycle at commit.
6. **Projector isolation:** ensure Core has no model/runtime dependency; Projector reads a versioned inventory snapshot.
7. **Question / Interest / Focus decoupling:** preserve their state machines and feed their internal signals to Cognitive Bias.
8. **Replay and failure injection:** test every transaction boundary, duplicate retry, stale inventory and snapshot-tail restore.
9. **Shadow evaluation:** compare legacy and minimal routes without letting the legacy schema define the new Core contract.
10. **Deprecation:** remove mandatory Attribute/Evidence/Relation generation only after the preceding gates pass.

## Release gates

- Schema validation and positive/negative fixture validation.
- C++ and Python admission tests against the same transaction semantics.
- Replay equivalence and idempotency under injected failures.
- Stale target/inventory rejection without partial mutation.
- Question / Interest / Focus regression on minimal-only streams.
- Independent Projector quality evaluation (precision/recall/selective risk).
- Target-device sustained throughput, peak RSS and p50/p95/p99 latency.

No feature in the table is marked implemented solely because the specification or schema exists. Update this map only after checking code, tests and CI for the exact commit.
