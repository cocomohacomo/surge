# SURGE Core Refactor Backlog v0.1

## Purpose

This document records implementation-level gaps observed in the current main
branch after the predicate-aware Core evaluator was merged. It is an
engineering backlog, not a claim that every item is a defect. Items are
ordered by dependency and risk.

## P0: transaction and state consistency

- [ ] Integrate `DependencyEngine::evaluate` into the staged post-SemanticDelta
  graph in `ContextGraph::prepare_events` and `ContextGraph::apply`.
- [ ] Persist triggered and rejected dependency outcomes in the same
  EventJournal transaction as Packet + SemanticDelta.
- [ ] Replay dependency outcomes after restart without incrementing state
  version per trigger event.
- [ ] Expose trigger results through C ABI and Python without a second,
  independently implemented predicate evaluator.
- [ ] Consolidate duplicated SemanticDelta validation/staging in
  `prepare_events` and `apply` behind one transaction-preparation path.
- [ ] Verify atomic packet admission and standalone SemanticDelta application
  have identical validation, idempotency, and replay semantics.

## P1: retrieval completeness and boundedness

- [ ] Replace the fixed 1000-record graph snapshot ceiling with cursor-based
  pagination or a versioned bounded query API.
- [ ] Add explicit context partition identity to persisted graph records and
  retrieval APIs; current isolation relies on one journal per context.
- [ ] Add lexical retrieval ranking tests for Japanese morphology, inflection,
  compound words, and short ambiguous queries; current matching is lexical
  substring/token baseline.
- [ ] Ensure multi-hop retrieval can report a resumable continuation when a
  hard budget is reached, rather than requiring a full snapshot.
- [ ] Measure retrieval latency and memory at increasing graph sizes.

## P1: dependency semantics

- [ ] Version the predicate grammar and introduce a typed predicate AST before
  adding temporal, numeric, Boolean, or negation expressions.
- [ ] Add explicit evaluation budgets for dependency records and predicate
  operations, not only impacted graph nodes.
- [ ] Define whether PRESENT/content/kind predicates are edge-triggered or
  level-triggered, and specify re-evaluation behavior when prerequisite data
  changes or is removed.
- [ ] Define deterministic handling for cyclic dependency components and
  non-convergence. SCC discovery exists; transactional fixpoint execution does
  not.
- [ ] Remove the duplicate Python predicate implementation after the C++ Core
  evaluator is exposed through a stable API.

## P2: semantic lifecycle and evidence quality

- [ ] Define when QuestionAspect resolution may be inferred from evidence and
  when it must remain an explicit external decision. Current status changes
  are explicit API operations.
- [ ] Define source trust, evidence freshness, temporal validity, and
  provenance-weight policies. These are not currently part of dependency
  evaluation.
- [ ] Add revision/supersession semantics for Claims and Evidence, including
  invalidation of dependent retrieval hints.
- [ ] Add conformance cases for quotation attribution, temporal scopes,
  conditional claims, negation scope, and revisions crossing packet
  boundaries.

## P2: integration and operational validation

- [ ] Add live-provider contract tests against an opt-in test endpoint; current
  automated tests use deterministic provider stubs.
- [ ] Add performance benchmarks for ingest, replay, retrieval, dependency
  evaluation, and memory use on realistic Japanese corpora.
- [ ] Add fault-injection tests for journal write interruption, duplicate
  event IDs, restart after commit, and concurrent optimistic-version races.
- [ ] Normalize schema inventory/reporting so CI reports the actual schema
  count consistently; historical logs have displayed an inconsistent
  `4/3 schemas valid` label despite successful validation.

## Current increment

- [x] Reject malformed dependency predicate operands and unknown node-kind
  names instead of silently treating malformed expressions as valid
  no-match predicates.
- [x] Add conformance tests for empty CONTENT_EQUALS, CONTENT_CONTAINS, KIND,
  and unknown KIND operands.

## Exit criteria

A backlog item is complete only when its implementation, regression tests,
documentation, and CI result are available. A PR being open or code merely
being drafted does not count as completion.

## Progress update: dependency transaction chain

PR #29 introduced atomic dependency trigger evaluation, PR #30 exposed durable
evaluation records through C ABI/Python, PR #31 added typed predicate parsing,
PR #32 added bounded SCC/fixed-point evaluation, and PR #33 added durable
fixed-point diagnostics. These PRs remain stacked and unmerged. The user
identified the earlier CI failures as GitHub token quota exhaustion; no source
failure is inferred from those runs. Build/test execution remains unverified.

## Progress update: Web document recognition v0.1

Branch `feat/web-document-recognition-regression-v0.1` adds
`WebDocumentRecognitionEngine`, UTF-8/NFC-aware chunking, typed marker-based
ScopeSpan candidates, conservative Claim applicability comparison, temporal
lifecycle classification, publisher/Evidence provenance, URL/content revision
indexing, and Python adapter regression tests. The engine prepares a typed
SemanticDelta and sends it through the existing atomic Packet + SemanticDelta
Core API. It does not change the production specification or the existing
Japanese Scoring 50-case dataset.

The exact prior Web test's individual 50 source excerpts and expected outputs
are not present in the retrievable conversation record. The known aggregate
(41 ACCEPT, 8 REVIEW, 1 ABSTAIN) and source grouping (MHLW, MAFF, MOE,
Tohoku University, MLIT/JMA) are not sufficient to reconstruct exact examples.
Per the user's explicit constraint, no replacement or invented 50-case corpus
is to be created. A JSON Schema for the separate Web regression dataset is
added; the exact cases must be imported from the original source material
before any 50-case pass rate is reported.
