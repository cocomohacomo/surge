# SURGE Streaming Text Runtime v0.1

## Purpose
This stage provides an executable Japanese-first text-to-context projection
behind a replaceable Recognizer protocol. It is a deterministic reference
runtime for semantic integration tests, not a production Japanese NLP model.

## Processing path
1. Accept one text segment in stream order.
2. A replaceable Recognizer emits typed candidates and source byte spans.
3. RecognitionScorer evaluates six independent dimensions.
4. The runtime associates candidates with topic clusters.
5. Explicit positive/negative claims with the same normalized proposition are
   both retained and linked by CONTRADICTS.
6. Contradiction emits a Question and directs Interest to the relevant cluster.
7. Ambiguous references and ABSTAIN decisions remain unresolved.
8. Short acknowledgements attach to the active cluster; they do not create
   standalone claims.
9. Topic changes create or reactivate a separate cluster.

## Authority and persistence boundary
The runtime owns only an in-memory projection. It does not write EventJournal,
create C++ SemanticDelta, or bypass Core validation. It is intentionally
separate from the authoritative C++ Core until the next integration stage
defines a typed, versioned SemanticDelta mapping and atomic/recoverable commit
protocol.

## Known limitations
- JapaneseRuleRecognizer is deliberately narrow and rule-based.
- Topic detection uses a small keyword lexicon.
- Proposition normalization is conservative and not a general semantic
  equivalence engine.
- Correction detection currently marks a revision candidate; it does not yet
  create a complete typed revision with verified old/new value spans.
- Scope for nested negation, quotations, concessions, tables and lists is not
  fully parsed.
- No calibrated quality, throughput, latency or memory claims are made.

## Required next gates
- Convert candidates into schema-valid ClassificationProposal using the
  Proposal Builder.
- Build typed Evidence/Claim/ScopeSpan/Relation/Question/Interest deltas.
- Validate state version, predecessor references and scope closure in Core.
- Commit packet plus semantic result with a recoverable transaction protocol.
- Replay the semantic graph from EventJournal and test the full 50-case corpus.
