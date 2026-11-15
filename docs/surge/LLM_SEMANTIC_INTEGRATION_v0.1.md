# LLM Interface and Semantic Core Integration v0.1

## Boundary

The LLM is an untrusted Recognition Candidate producer. It has no Core handle,
journal credentials, tools, or commit authority. The adapter treats source text
as data, requires a strict JSON object, rejects unknown fields and invalid
scores, then applies the existing six-axis Recognition Scoring policy.

The provider interface is a small `LLMProvider.recognize(text)` protocol.
`OpenAICompatibleProvider` uses the Chat Completions JSON Schema response
format over the Python standard library. It reads credentials from
`OPENAI_API_KEY` unless explicitly injected. Tests use a deterministic stub
provider and never call an external model.

## Execution path

1. Text is normalized to NFC UTF-8 and admitted as a RecognitionPacket.
2. The LLM receives the current ContextHint version, active topic, previous turn kind and up to eight recent claims. This gives short acknowledgements a bounded context window. It emits kind, topic, proposition, polarity, conditionality and three
   calibrated-input candidates (confidence, ambiguity, importance).
3. The adapter validates the exact key set, enum values, finite [0,1] scores,
   and required claim fields.
4. RecognitionScorer combines the three LLM dimensions with explicit routing
   defaults for relevance, relation strength and novelty.
5. Evidence is always retained, including ABSTAIN cases. A Claim is created
   only for a non-abstained complete claim candidate.
6. Claim references Evidence; ABOUT connects Claim to its topic Cluster.
   Opposite-polarity exact-proposition claims create CONTRADICTS, Question,
   QuestionAspect and Interest nodes.
7. A typed SemanticDelta is passed through the new C ABI to C++ ContextGraph.
   Core validates references and relation kinds, stages the graph, and commits
   all graph events in one EventJournal transaction.
8. C++ replays packet admission and semantic graph events from the same journal
   after restart.

## Safety and limits

- Model output never bypasses Core validation.
- The prompt explicitly marks user text as untrusted input data; no tools are
  exposed to the model.
- No semantic winner is selected for contradictory claims.
- Low-confidence/ambiguous claims are retained as Evidence but not committed
  as resolved Claims.
- The first interface stores topic clusters per packet and keeps contradiction
  matching in the live Python session. Persistent graph lookup for cross-process
  cluster reuse and contradiction matching is a follow-up.
- ScopeSpan is not yet a first-class ContextGraph node. Conditionality is
  retained in Claim content, but full typed scope-anchor validation and
  atomic ScopeSpan revision are not part of this adapter stage.
- The OpenAI-compatible provider has not been exercised against a live API in
  CI. A deterministic provider is used for reproducible integration tests.
- Packet admission and semantic commit remain separate journal transactions.
  If semantic commit fails after packet admission, the caller must retry the
  same source item with the same packet identity before advancing the stream.

## Test command

```sh
python -m unittest discover -s tests -p 'test_llm_semantic_integration.py' -v
```
