# LLM Proposal Adapter Refactor v0.2

## Purpose

Separate proposal construction and Claim acceptance policy from the large
semantic-runtime orchestration method. This is a structural refactor: it
preserves the provider contract, Recognition Scoring policy, packet format,
SemanticDelta shape, and Core commit boundary.

## Refactored responsibilities

- `LLMSemanticRuntime._build_proposal` maps a validated Recognition Candidate
  and scoring result into a schema-validated, non-committing
  ClassificationProposal.
- `LLMSemanticRuntime._accepts_claim` centralizes the rule that only
  `ACCEPT` may create a resolved Claim. `REVIEW` and `ABSTAIN` cannot.
- `process` orchestrates recognition, scoring, proposal creation, graph
  staging, and atomic packet-plus-semantic admission without embedding the
  proposal schema mapping inline.

## Invariants

1. Proposal schema validation completes before SemanticDelta construction.
2. Proposal construction does not mutate Core or EventJournal.
3. The proposal base state version is the same Core state version used by the
   packet ContextHint and SemanticDelta expected-state version.
4. The C++ Core remains the only authority that commits SemanticDelta.
5. Evidence is retained for REVIEW and ABSTAIN; a resolved Claim is emitted
   only for ACCEPT.
6. Missing topic metadata produces no synthetic topic label.
7. No source offsets are fabricated. Candidate spans remain empty until the
   recognizer can provide verified packet-relative UTF-8 byte offsets.

## Validation

Run:

```sh
python -m unittest discover -s tests -p 'test_llm_proposal_integration.py' -v
```

The deterministic tests cover proposal schema validity, packet/state lineage,
ACCEPT/REVIEW/ABSTAIN behavior, absent-topic handling, and the isolated Claim
gate. Live provider behavior is intentionally outside this test suite.

## Deferred work

Durable proposal audit/replay is implemented in the subsequent
`CLASSIFICATION_PROPOSAL_EVENTJOURNAL_v0.1.md` increment. The Python adapter
passes the validated proposal into the existing atomic packet-plus-semantic
transaction; EventJournal remains the single persistence authority.
