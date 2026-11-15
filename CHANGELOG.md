# Changelog

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
