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


## Progress update

The first P0 Core transaction increment is implemented on branch
`feat/atomic-dependency-trigger-journal-v0.1` and proposed in PR #29.
Dependency evaluation results are journaled atomically with SemanticDelta and
restored on replay. Restart, idempotency, and atomic packet + semantic tests
have been added. The current CI attempts have not produced a successful run,
so these checklist items remain open until the test suite passes and the PR is
merged. C ABI/Python exposure and fault-injection verification are also open.


A follow-up implementation is in progress on
`feat/dependency-evaluation-c-api-v0.1`, stacked on PR #29. It adds a
read-only C ABI and Python `ContextRetriever` API for retrieving persisted
dependency evaluation records by delta ID, with bounded-buffer retry and
restart-replay conformance coverage. This work is not complete until the
stacked PR's CI passes and both PRs are merged.
