# SURGE Library Rollout Gate v0.1

Date: 2026-11-21

This document separates a usable source-built library from a production-ready
general-purpose SURGE release. A passing unit-test suite alone is not a release
decision.

## Current release shape

- Native Core: C++17 shared library with a C ABI.
- Build system: CMake.
- Python: adapter modules are currently source-tree modules; no standalone
  wheel is published yet.
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
- [ ] Linux, macOS, and Windows install/consumer tests all green on the same
  commit.
- [ ] ABI versioning and compatibility policy documented and tested.
- [ ] Install-tree inspection confirms no build-tree paths leak into exports.

### Gate B: reproducible developer integration

- [x] Build and CTest instructions.
- [x] Schema/fixture and Python regression CI.
- [ ] Standalone Python package metadata and wheel build.
- [ ] Python package locates the installed native library deterministically.
- [ ] Clean-environment install test with no repository checkout on
  `PYTHONPATH`.
- [ ] Versioned public C ABI header and API compatibility test.

### Gate C: persistence and recovery safety

- [x] Journal framing, checksums, replay and truncated-tail handling.
- [x] Backup/verify tooling and snapshot primitives.
- [ ] Explicit power-loss durability contract per supported filesystem/OS.
- [ ] Multi-process writer policy enforced or clearly documented as unsupported.
- [ ] Full domain snapshot/restore coverage, including histories and atomic
  cross-domain rollback.
- [ ] Backup restore tested from a separate process and clean installation.
- [ ] Corruption, disk-full, permission, interrupted-write and rollback tests.

### Gate D: bounded edge operation

- [x] Logical resident/transient/event/cluster/candidate/evidence budgets.
- [x] Guarded C ABI mutation gateway.
- [ ] All production mutation paths routed through guarded APIs.
- [ ] Peak RSS measured under representative long-running streams.
- [ ] Restore, compaction and backup peak-memory measurements.
- [ ] Stable memory bound demonstrated under adversarial input and repeated
  sessions.
- [ ] RTF, p95/p99 latency and throughput benchmarks on declared reference
  hardware.

### Gate E: semantic quality and language readiness

- [x] Typed graph, Question/Interest/Focus, dependency and temporal primitives.
- [x] Japanese scoring regression corpus and deterministic policy tests.
- [ ] Independent gold corpus with annotator agreement and provenance.
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
- [ ] Versioned release artifacts, checksums and SBOM.
- [ ] Third-party license inventory and redistribution review for every
  shipped artifact.
- [ ] Security contact and vulnerability response process verified.
- [ ] Release notes, migration guide, supported platform matrix and rollback
  instructions.
- [ ] Tagged release built from a green, reviewed commit.

## Current stop-ship conditions

Do not label SURGE a production-ready general-purpose library until Gates A-F
have evidence for their required checks. The CMake installation path is an
initial native-library integration surface, not a binary release or a claim
that all cognitive features are complete.

Do not merge PR #139 or change repository visibility as part of this rollout
work unless the repository owner explicitly requests that action.
