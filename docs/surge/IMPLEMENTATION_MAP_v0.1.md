# SURGE v0.9 Implementation Map v0.1

**Snapshot:** main `9dd72cd8eac843083fd62b2966e049fde85b588d` (2026-11-17)  
**Normative target:** `SPECIFICATION_v0.9.md`  
**Purpose:** one-page entry point for adopters. A feature is "implemented" only when code, regression coverage and a passing CI path are present. Open PRs and design documents alone do not count.

## Status at a glance

| Area | v0.9 expectation | Main-branch status | Boundary / next work |
|---|---|---|---|
| RecognitionPacket | identity, byte ranges, digest, sequence, idempotency | **Implemented** | Python validates schema/NFC; C++ independently checks UTF-8 and SHA-256 |
| Packet admission | causal sequence, gaps, duplicate/conflict, replay | **Implemented, single-process** | no multi-process writer lock or IPC |
| SemanticDelta / typed graph | staged validation, atomic journal commit, replay | **Implemented core subset** | not every normative cross-record invariant is closed |
| Question / Interest / Focus | contradiction routing, safe focus | **Implemented subset** | semantic discovery is supplied by recognizer; no autonomous text understanding claim |
| Dependencies | impact set, predicate checks, SCC detection | **Partial** | bounded fixed-point and durable diagnostics remain in open PRs #29-33; not main-branch capability |
| ScopeSpan | typed validation and anchored spans | **Partial** | 11 types represented; full linguistic extraction and all cross-packet lifecycle invariants remain open |
| ContextHint / RetrievalTicket | mandatory references, budget, stale check | **Partial to implemented** | persistent ticket path exists; authorization, expiry/supersede and graph-wide query coverage require further conformance |
| Claim / Evidence temporal validity | append-only intervals, point-in-time state | **Implemented subset** | wider source provenance and propagation policy need evaluation |
| Evidence provenance | observed/published/retrieved clocks, revisions, atomic packet integration | **Implemented in main** | clocks are caller-supplied; no timestamp inference |
| Recognition scoring | ACCEPT / REVIEW / ABSTAIN | **Implemented policy boundary** | 50 authored regression cases are not an independent gold corpus |
| Recognition Engine | text-to-Claim/ScopeSpan extraction | **Partially integrated** | provider adapter exists; independent semantic accuracy not established |
| Benchmark / quality gate | immutable profile, coverage, risk, critical recall, invariants | **Evaluator implemented, corpus absent** | meeting-decision JSONL adapter/evaluator and schemas are present; no independent gold corpus or comparative result yet |
| Persistence / operations | crash durability, lock, backup, migration | **Prototype only** | fsync, process lock, snapshot/compaction, restore drill, migration guarantees incomplete |
| Model performance | end-to-end RTF, p99, RSS | **Unmeasured** | no production claim until measured on declared hardware/model |

## What currently runs

- C++17 shared Core, C ABI and Python ctypes bridge.
- Packet admission, typed SemanticDelta, journal replay and duplicate retry.
- ContextGraph, QuestionRouter, DependencyEngine, ScopeSpanValidator and ContextHint paths.
- Claim/Evidence temporal ledgers, reassessment history and Evidence provenance.
- Python RecognitionPacket and LLM adapters, deterministic provider tests.
- CMake/CTest, Draft 2020-12 schema and fixture validation in GitHub Actions.\n- 14 C++ test programs now use a Release-safe `CHECK` assertion mechanism (532 checks); a deliberate-failure probe proves checks remain active with `NDEBUG`.\n- Meeting-decision adapter runner, deterministic explicit-field smoke adapter, evidence-span evaluator and gold/prediction JSON Schemas.

## What is not evidence of semantic quality

- D01-D20 are structural/conformance fixtures; they are not 20 end-to-end semantic accuracy trials.
- The 50 Japanese scoring examples are policy regression cases; they are not independently labelled production data.
- CI passing proves exercised code paths pass tests. It does not prove extraction precision/recall, end-to-end RTF, p99 latency, peak RSS, multi-process safety or power-loss durability. The current independent-gold corpus is intentionally empty.
- A PR branch, a design document or a green workflow on an unmerged branch is not a main-branch capability.

## Recommended adoption path

1. Start with one benchmark use case: Japanese meeting decision/action extraction.
2. Use a common JSONL adapter contract so SURGE, simple RAG and structured-DB baselines see identical source snapshots.
3. Obtain independently double-annotated gold data, adjudicate disagreements, freeze the split and profile digest before evaluation.
4. Compare semantic metrics and performance separately; include abstention coverage and selective risk.
5. Only after reproducible benefit is shown, close multi-process locking, crash durability, backup/restore and migration gaps before production deployment.

Latest verified main CI: 15/15 CTest targets, 168/168 Python tests, 5/5 v0.9 schemas and 20/20 fixture structural contracts. The main CI run is associated with the snapshot commit above.\n\nSee [Meeting Decision Evaluation Protocol v0.1](../evaluation/MEETING_DECISIONS_EVALUATION_v0.1.md).
