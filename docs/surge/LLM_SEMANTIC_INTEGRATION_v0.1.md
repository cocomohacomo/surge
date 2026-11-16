# LLM Interface and Semantic Core Integration v0.2

## Boundary

The LLM is an untrusted Recognition Candidate producer. It has no Core handle,
journal credentials, tools, or commit authority. The adapter treats source text
as data, requires a strict JSON object, rejects unknown fields and invalid
scores, and applies the six-axis Recognition Scoring policy.

The provider interface is `LLMProvider.recognize(text, context)`.
`OpenAICompatibleProvider` uses Chat Completions JSON Schema over the Python
standard library. Tests use deterministic providers and do not call a live
external model.

## Execution path

1. Text is normalized to NFC UTF-8 and represented as a RecognitionPacket.
2. The LLM receives the current ContextHint version, active topic, previous
   turn kind, and up to eight recent claims. It emits kind, topic, proposition,
   polarity, conditionality, confidence, ambiguity, and importance.
3. The adapter validates the exact key set, enum values, finite [0,1] scores,
   and required Claim fields.
4. RecognitionScorer evaluates six dimensions. Relevance, relation strength,
   and novelty are supplied by explicit runtime rules; confidence, ambiguity,
   and importance are supplied by the recognizer.
5. ClassificationProposal Builder maps the Recognition Candidate and score
   result into the v0.9 ClassificationProposal schema. Packet lineage,
   selected score dimensions, abstention reasons, and review warnings are
   validated before any SemanticDelta is assembled.
6. The proposal remains untrusted and non-committing. It is returned as part
   of the runtime result for audit and downstream review. It does not grant
   commit authority and is not itself persisted by the current Core API.
7. Evidence is retained, including ABSTAIN cases. A resolved Claim is created
   only when the scoring decision is ACCEPT. REVIEW carries
   MANUAL_REVIEW_REQUIRED and ABSTAIN carries its abstention reasons; neither
   commits a resolved Claim.
8. Claim references Evidence; ABOUT connects Claim to its topic Cluster.
   Opposite-polarity exact-proposition Claims create CONTRADICTS, Question,
   QuestionAspect, and Interest nodes.
9. A typed SemanticDelta is passed through the C ABI to C++ ContextGraph.
   Core validates references and relation kinds, stages the graph, and commits
   graph events through EventJournal.
10. C++ replays packet admission and semantic graph events from the journal
    after restart.

## Safety and limits

- Model output never bypasses Core validation.
- The prompt marks user text as untrusted input data; no tools are exposed to
  the model.
- No semantic winner is selected for contradictory Claims.
- REVIEW and ABSTAIN proposals are not committed as resolved Claims. Evidence
  remains available for later review or context processing.
- Proposal validation is a pre-Core adapter gate, not a replacement for Core
  validation. Core remains the sole authority for SemanticDelta acceptance.
- The current LLM response has no token-level offsets, so its proposal has
  typed labels but no candidate spans. A span-producing recognizer may populate
  candidate_spans only with verified packet-relative offsets.
- The current proposal is returned to the caller but is not yet durably stored
  in EventJournal. Durable proposal audit/replay is a separate follow-up.
- Topic clusters are still created per packet and contradiction matching
  remains session-local in this runtime. Persistent cross-session retrieval
  and cluster reuse are separate follow-ups.
- Typed ScopeSpan generation from LLM output is not yet connected here.
- The OpenAI-compatible provider has not been exercised against a live API in
  CI. Deterministic provider fixtures are used for reproducible tests.

## Test command

```sh
python -m unittest discover -s tests -p 'test_llm_proposal_integration.py' -v
```
