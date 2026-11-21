# SURGE Library Rollout Gate v0.1

Date: 2026-11-21

This document separates a usable source-built library from a production-ready
general-purpose SURGE release. A passing unit-test suite alone is not a release
decision.

## Current release shape

- Native Core: C++17 shared library with a C ABI.
- Build system: CMake.
- Python: a standalone adapter wheel builds and installs in an isolated
  environment; the wheel does not bundle the native Core binary. The native
  library remains a separately installed artifact.
- Core state: journal-backed, single-process writer model. Multi-process
  writer coordination is not supported.
- Resource limits: logical admission budgets are available through the
  guarded C ABI. They are not an operating-system RSS cap.
- Language runtime: registry and manifest metadata are implemented, but
  complete package loading, signature verification, and executable language
  resource activation are not complete.
- Archive restore: graph/current Focus restoration is limited. Non-empty
  unsupported history domains fail closed rather than being silently dropped.

## Rollout gates

### Gate A: installable native library

- [x] CMake install rules for shared library and public headers.
- [x] Export relocatable CMake package target `surge::surge_core`.
- [x] External consumer smoke project.
- [x] Linux, macOS, and Windows install/consumer tests all green on the same
  commit (verified by the cross-platform CI matrix).
- [x] Provisional runtime ABI version query and compatibility policy documented.
- [x] Source-level C ABI declaration manifest and CI diff guard (function signatures and public struct field order/types).
- [x] Platform binary export presence and public struct layout inspection on Linux, macOS, and Windows (CI; records actual target layouts, not a cross-version compatibility baseline).
- [x] Install-tree inspection confirms no build-tree paths leak into exports (checked by CI outside the source tree).

### Gate B: reproducible developer integration

- [x] Build and CTest instructions.
- [x] Schema/fixture and Python regression CI.
- [x] Standalone Python package metadata and wheel build in CI.
- [x] Python package locates the installed native library deterministically via `SURGE_CORE_PREFIX` / `SURGE_CORE_LIBRARY`.
- [x] Reviewed C ABI declaration surface is checked in CI.
- [x] Clean-environment install test with no repository checkout on
  `PYTHONPATH`, using an isolated wheel and separately installed native Core.
- [x] Public C ABI version macros, runtime query, and conformance test.
- [x] Complete public C ABI function export manifest checked against built shared libraries on Linux, macOS, and Windows.
- [x] Version-to-version ABI compatibility automation against immutable ABI baselines.
- [ ] Retained platform-specific layout baselines across compiler and architecture versions.

### Gate C: persistence and recovery safety

- [x] Journal framing, checksums, replay and truncated-tail handling.
- [x] Backup/verify tooling and snapshot primitives.
- [x] Explicit power-loss durability contract per supported filesystem/OS (software barriers and limits documented; hardware power-cut qualification remains deployment-specific).
- [x] Multi-process writer exclusion enforced with OS locks and documented as single-writer ownership.
- [x] SnapshotCoordinator restore coverage for all five registered domains: ContextGraph (including temporal/dependency/reassessment histories and idempotency), EvidenceProvenance, ChaosCluster (including lifecycle, membership, observation, association and hypothesis histories), TemporalBoundary, and TemporalQuestion.
- [x] Prepared restore publication is reversible: if a later domain refuses commit, already-published domains roll back in reverse order. Failure-injection tests cover direct restore, checkpoint-plus-tail restore, and store-backed restore.
- [x] ClusterArchiveStore v2 restore imports graph nodes/relations, Focus history, ScopeSpan records, Claim/Evidence validity histories, dependency evaluations, temporal reassessments, Question/QuestionAspect state and Claim lifecycle in one durable ContextGraph journal transaction; restart replay is covered by tests.
- [ ] Archive-store publication plus graph detachment is still a staged two-resource operation: a failed graph detach leaves the verified archive copy available for recovery while preserving the source graph; archive publication is not rolled back atomically.
- [x] Backup restore tested through the separately installed native verifier in a fresh temporary directory and separate process on Linux, macOS, and Windows.
- [ ] Corruption, disk-full and permission fault injection across all persistence paths.
- [x] Snapshot writer exclusion and pre-/post-atomic-replacement failure recovery tests.

### Gate D: bounded edge operation

- [x] Logical resident/transient/event/cluster/candidate/evidence budgets.
- [x] Guarded C ABI mutation gateway.
- [ ] All production mutation paths routed through guarded APIs.
- [x] CI EventJournal profile records append percentiles, replay time, identity-index usage, and peak RSS for a bounded 10,000-event workload.
- [ ] Peak RSS measured under representative long-running semantic streams on declared reference hardware.
- [ ] Restore, compaction and backup peak-memory measurements.
- [ ] Stable memory bound demonstrated under adversarial input and repeated
  sessions.
- [ ] RTF, p95/p99 latency and throughput benchmarks on declared reference
  hardware.

### Gate E: semantic quality and language readiness

- [x] Typed graph, Question/Interest/Focus, dependency and temporal primitives.
- [x] Japanese scoring regression corpus and deterministic policy tests.
- [x] Gold-corpus validator computes inter-annotator action F1, owner/deadline/criticality agreement, and exact evidence-span agreement against declared thresholds.
- [ ] Independent gold corpus with real annotators, agreement results, and source provenance.
- [ ] Precision/recall/F1, selective risk, critical false-negative rate and
  abstention calibration measured.
- [ ] Streaming continuation, correction, contradiction, topic switch and
  cross-cluster agreement/conflict evaluated end-to-end.
- [ ] Japanese production adapter demonstrated; second-language packs remain
  separate conformance work.
- [ ] No claim of language-independent recognition until independent
  language-specific evaluation passes.

### Gate F: release operations

- [x] MIT license and security policy.
- [x] Release checklist and publication audit records.
- [x] CI-generated CycloneDX 1.5 SBOM for the Python wheel dependency graph and separately installed native Core (retained as a CI artifact).
- [ ] Versioned release artifacts, checksums and SBOM attached to a tagged release.
- [x] Source-level third-party dependency inventory and resolved license metadata captured in the CI SBOM.
- [ ] Legal redistribution review for every shipped artifact and all transitive dependencies.
- [ ] Security contact and vulnerability response process verified.
- [ ] Release notes, migration guide, supported platform matrix and rollback
  instructions.
- [ ] Tagged release built from a green, reviewed commit.

## Cluster archive restore transaction

ClusterArchiveStore v2 payloads are validated in full before a restore candidate is built. The importer merges graph nodes and relations plus ScopeSpan, Focus history, Claim/Evidence validity, dependency evaluation, temporal reassessment, Question state/reference counts, QuestionAspect status and Claim lifecycle. The import is persisted as one `CLUSTER_ARCHIVE_IMPORT_V1` journal event, so replay either reconstructs the full imported graph state or rejects the event; partial history import is not published. BudgetedContextGraph reserves transient staging, resident, retained-event and cluster capacity before journal commit. The host must quiesce graph readers/writers during the operation. Archive-store publication and graph detachment remain two-resource staged operations: failed detach preserves the source graph and leaves the verified archive copy available for recovery. CI run 44888865145 passed all 9 jobs on the archive import implementation, including full C++ tests, ASan/UBSan, fuzz smoke and Linux/macOS/Windows.

## Snapshot restore transaction contract

`SnapshotCoordinator` prepares every registered domain without mutating live state. Each prepared adapter must implement a no-throw reversible publication: a failed `commit()` must leave its own domain unchanged, and `rollback()` must restore the exact pre-commit state. If publication fails, the coordinator rolls back previously committed domains in reverse registration order. The host must quiesce readers and writers across all registered domains for the full restore operation; rollback guarantees failure recovery, while quiescence prevents readers from observing intermediate cross-domain publication.

This contract is verified for `restore`, `restore_with_tail`, and `restore_from_store`. It does not close the separate `ClusterArchiveStore` history-domain restoration gap or establish hardware power-loss qualification.

## Current stop-ship conditions

Do not label SURGE a production-ready general-purpose library until Gates A-F
have evidence for their required checks. The CMake installation path is an
initial native-library integration surface, not a binary release or a claim
that all cognitive features are complete.

Do not merge PR #139 or change repository visibility as part of this rollout
work unless the repository owner explicitly requests that action.
