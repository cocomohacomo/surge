# Chaos Cluster v1.0 implementation audit

Audit baseline: main `06daf8ac411d861539e9d65f3ee3a9b6090b989c` plus
the Focus retrieval integration merged immediately afterward.
Normative references: `SPECIFICATION_v1.0.md`,
`CHAOS_CLUSTER_MODEL_v1.0.md`, and
`schemas/surge-chaos-cluster-v1.0.schema.json`.

This is a source-level implementation audit, not a security certification or
independent semantic-quality evaluation.

## Status definitions

- **Implemented**: native code, exposed API where applicable, and regression
  coverage exist.
- **Partial**: a meaningful subset exists, but one or more normative contracts
  or public interfaces are missing.
- **Missing**: no corresponding implementation was found in the audited path.
- **Unverified**: source suggests behavior, but required failure/concurrency/
  durability evidence has not been run.

## Findings

| Priority | Requirement | Status | Gap |
|---|---|---|---|
| P0 | Orthogonal score observations | Partial | Schema defines cluster observations and member salience / association confidence / hypothesis confidence. Initial native ledger tranche stores cluster-level observations only; adapters, bounded projection and retrieval consumption are missing. |
| P0 | Membership status and unmembership | Missing | Native member record has identity/provenance but no membership status or append-only removal/reinstatement edge history. |
| P1 | PatternHypothesis auditability | Partial | Member/excluded/counterexample/alternative refs persist. Exclusion reasons, confidence observations and append-only hypothesis review revisions are absent. |
| P1 | Association valid-time | Partial | Status revisions include provenance and terminal EXPIRED exists. valid_from/valid_until are not persisted and expiry is not automatically evaluated. |
| P1 | Retrieval diversity | Partial | Focus retrieval round-robins admission-reason groups. It does not rank or constrain by persisted novelty, salience, diversity, ambiguity or utility observations. |
| P1 | Retrieval query dimensions | Partial | Bounded full-cluster projection and typed Focus retrieval exist. Dedicated filtering by association type/status, temporal range, provenance, hypotheses/counterexamples and lineage traversal is incomplete. |
| P1 | Lifecycle and operations | Partial | Transition table includes ARCHIVED -> DORMANT. Dedicated audited reactivation, merge, and lifecycle transition history as first-class records are missing. Fork/split/subset promotion exist. |
| P1 | CC01-CC20 executable conformance | Partial | C++/C ABI/Python tests cover selected persistence, replay, lifecycle, association, hypothesis, promotion/fork/split and Focus paths. The complete 20-case semantic matrix is not yet executed end-to-end. |
| P2 | Memory and forgetting | Partial/Missing | Lifecycle dormancy/archive exists, but manifest compaction, inspectable eviction factors, recoverable archive workflow, payload tombstone policy and restore drill are not complete. |
| P2 | Operational safety | Unverified | Multi-process writer coordination, concurrent Core-handle mutation contract, hostile-input fuzzing, sanitizers, crash/power-loss and backup/restore drills remain outstanding. |
| P2 | Performance | Unverified | No declared-hardware measurement proves 30 MiB model integration, RTF <0.01, p99 latency or peak RSS for the v1.0 path. |

## Implementation order

1. Persist scorer-bound orthogonal observations with append-only history and
   backward-compatible replay. Do not collapse dimensions or treat missing as
   zero.
2. Expose bounded observation read/write through C ABI and Python; test restart
   parity and reject invalid dimensions, NaN/Infinity, out-of-range values and
   malformed provenance.
3. Add member membership status and append-only unmembership/reinstatement.
4. Add PatternHypothesis review/exclusion rationale and association valid-time.
5. Implement score-aware retrieval with independently configurable novelty,
   salience and diversity constraints, explicit ranking rationale and omissions.
6. Close all CC01-CC20 assertions, including negative and restart cases.
7. Complete merge/reactivation, retention manifests/tombstones, and operational
   hardening before production claims.

## First implementation tranche

The score-observation persistence work is being delivered separately from this
audit. It is intentionally limited to the native C++ ledger first. It does not
claim full score observation conformance until the C ABI/Python adapters,
bounded projection and score-aware retrieval are implemented and tested.
