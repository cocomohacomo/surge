# Changelog

## 0.9.9

- Add C++ PacketAdmissionCoordinator to stage SourceSequencer and RangeLedger changes before a single EventJournal commit.
- Publish in-memory admission state only after successful journal commit.
- Rebuild packet identity, idempotency, source sequence and range state from committed journal events on restart.
- Add conformance coverage for exact retries, sequence gaps, range conflicts, restart replay and failed-commit non-publication.
- Document trusted-adapter boundary and remaining lack of a Python/C++ binding and C++ SHA-256 recomputation.


## 0.9.8

- Add Python RecognitionPacket admission adapter with Draft 2020-12 schema validation.
- Verify NFC UTF-8 snapshot digest, half-open byte ranges and exact source-range SHA-256.
- Enforce packet identity, idempotency, per-generation sequence uniqueness and append-only chained revision lineage.
- Add 12 focused adapter conformance tests to the standard fixture-validation command.
- Document that adapter admission remains process-local and is not yet atomically integrated with the C++ Core/EventJournal.


## 0.9.7

- Add deterministic ContextHint candidate priority, deduplication and bounded reference selection.
- Preserve mandatory-reference overflow as context_complete=false and issue idempotent RetrievalTickets.
- Exclude optional omissions from mandatory RetrievalTicket payloads.
- Add state-version-aware read-only ticket resolution with replay or STALE behavior.
- Add seven C++ conformance groups.


## 0.9.6

- Add type-specific ScopeSpan validation for all 11 v0.9 scope kinds.
- Validate normalized UTF-8 byte anchors, digest/offset metadata, packet lineage and target bindings.
- Preserve unresolved condition/concession/contrast/quotation scopes and ambiguous coreference/anaphora.
- Prevent negation overreach, unconditionalization of conditionals and modality/factuality conflation.
- Add 12 C++ scope conformance groups.


## 0.9.5

- Add deterministic contradiction routing from explicit Claim-to-Claim CONTRADICTS relations.
- Create Question and QuestionAspect records and route Interest to all linked Clusters.
- Select Focus only for a single unambiguous target Cluster; preserve ambiguity when multiple Clusters are involved.
- Make routing idempotent by contradiction relation and add multi-cluster/single-cluster tests.


## 0.9.4

- Add bounded transitive Dependency impact-set computation with deterministic ordering and budget enforcement.
- Add Tarjan SCC detection for cyclic dependency groups.
- Mark budget-exceeded results incomplete and prohibit treating partial impact sets as complete.
- Add dependency engine tests for chains, cycles, determinism, missing roots and invalid edges.


## 0.9.3

- Add typed ContextGraph nodes (Cluster, Entity, Evidence, Claim, Question, QuestionAspect, Dependency, Interest) and relations.
- Add staged SemanticDelta validation and atomic EventJournal commit/publish semantics.
- Add cluster Focus and Interest-to-QuestionAspect/Cluster binding with append-only focus history.
- Add committed-event replay and idempotency restoration across restart.
- Add ContextGraph conformance tests for type constraints, stale versions, idempotent retry and multi-transaction recovery.


## 0.9.2

- Add an append-only framed event journal with commit markers, state-version checks, event-ID uniqueness and idempotent transactions.
- Recover committed transactions on restart; discard incomplete/uncommitted tail records and fail closed on complete CRC mismatches.
- Add journal conformance tests for restart, retries, conflict handling, truncated-tail recovery and checksum corruption.
- Document that CRC/FNV are not cryptographic and stream flush is not an OS-level power-loss durability barrier.


## 0.9.1

- Add a C++17 Core prototype for source sequencing, gap lifecycle, range idempotency/revisions, ScopeSpan transitions, RetrievalTicket staleness and quality/performance gates.
- Add CMake/CTest unit tests and GitHub Actions conformance workflow.
- Add Python Draft 2020-12 schema and D01-D20 fixture consistency validation.
- Add implementation status and explicit boundaries for the prototype.
- No model accuracy, RTF, p99 latency or peak RSS claims.

## 0.9.0

- Closed source sequence gap lifecycle with authorized skip audit and late-arrival recovery.
- Defined exact source range digest identity, duplicate retry, content conflict and append-only revision rules.
- Defined type-specific ScopeSpan closure validation and atomicity.
- Defined deterministic mandatory ContextHint priority and RetrievalTicket revalidation.
- Added immutable pre-registered benchmark profile, denominator rules, coverage/selective-risk metrics and dual gates.
- Added D13-D20 fixtures and made D01-D12 executable-oriented JSON fixtures.
- Updated README current-specification references.
- No production Core implementation or measured performance claim is included.

## [0.9.10] - 2026-11-16

- Add a dependency-free C++ SHA-256 implementation and strict UTF-8 validation.
- Require PacketAdmissionCoordinator to independently verify the complete NFC UTF-8 snapshot digest and exact byte-range digest before staging or committing an admission.
- Remove caller-controlled validation flags from the PacketAdmission API.
- Write RPA2 packet journal payloads while retaining RPA1 replay compatibility.

## [0.9.11] - 2026-11-16

- Build the C++ Core as a shared library and expose a stable C ABI for journal-backed RecognitionPacket admission.
- Add Python ctypes `CppCoreBridge` and `RecognitionPacketAdapter.admit_to_core()`; C++ is the durable admission authority.
- Add end-to-end integration coverage for NFC normalization, schema rejection, durable commit, duplicate retry and restart replay.
- Run the Python/C++ integration suite in GitHub Actions after building the shared Core.
