# LLM ClassificationProposal Integration v0.2

This document supersedes the execution-path description in
`LLM_SEMANTIC_INTEGRATION_v0.1.md` while preserving that file as a historical
design record.

## Normative execution order

`Text -> RecognitionPacket -> Recognition Candidate -> Recognition Scoring
-> ClassificationProposal -> SemanticDelta -> Core Validator -> EventJournal`

1. Recognition output is strictly validated before scoring.
2. RecognitionScorer evaluates the six declared dimensions.
3. Proposal Builder creates a complete v0.9 ClassificationProposal, including
   packet lineage, model/runtime descriptors, score-backed labels, and
   abstention/review metadata.
4. JSON Schema validation must succeed before the runtime constructs a
   SemanticDelta.
5. The proposal is untrusted metadata. Only the Core may validate and commit
   the SemanticDelta.
6. ACCEPT may create a resolved Claim. REVIEW and ABSTAIN retain Evidence but
   do not create a resolved Claim.
7. The current provider response contains no verified source offsets, so
   candidate_spans remains empty. Future span emission must use absolute
   half-open offsets in the packet's declared offset basis.
8. The runtime returns the validated proposal alongside commit results. The
   current EventJournal does not persist proposal objects; durable proposal
   audit and replay require a later Core/API change.

## Conformance

The integration tests verify schema-valid proposal construction, packet and
state-version lineage, ACCEPT commit behavior, REVIEW gating, and ABSTAIN
metadata propagation. The deterministic test provider does not call an
external LLM endpoint.
