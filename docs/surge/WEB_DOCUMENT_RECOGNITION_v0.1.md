# Web Document Recognition Engine v0.1

## Purpose and separation

This layer connects web-document text to the existing RecognitionPacket,
RecognitionScoring, typed ScopeSpan and journal-backed Core path. The provider
is an untrusted candidate producer. It cannot write to Core. Existing
`SURGE_JA_SCORING_50_v0.1.json` remains a separate scoring-only dataset.

The Web regression corpus has its own IDs, source records, expected outputs and
runner. A URL register is not itself a regression corpus: every executed case
must retain the acquired text excerpt, exact URL, capture time, content digest,
expected Claim/ScopeSpan, expected lifecycle state and adjudicated outcome.

## 1. Claim applicability comparison

A contradiction is eligible only when normalized propositions match, topic and
geographic scope do not conflict, condition scopes overlap, validity intervals
overlap, and opposite known polarities remain after temporal lifecycle checks.
The comparator returns a reason code and never resolves a conflict by choosing
a winner. Planned-versus-cancelled is a lifecycle transition, not a contradiction.

Current v0.1 scope dimensions: proposition, polarity, topic, condition markers,
geography, temporal state, valid-from/to and modality. Missing dimensions are
unknown, not wildcards with evidentiary certainty. The comparator is
conservative and returns no contradiction when scope cannot be shown to overlap.

## 2. Temporal lifecycle

Recognized states are PLANNED, FORECAST, UNDER_REVIEW, DECIDED, CURRENT,
COMPLETED, CANCELLED and UNKNOWN. They are independent of polarity and
confidence. A later document creates a new immutable claim occurrence and
records supersession metadata; it does not rewrite the earlier claim. State
transition ordering is not inferred solely from publication date.

## 3. Web document identity and deduplication

Canonical URL removes fragments, query strings and default path differences.
The SQLite index stores URL, source digest, packet identity, revision sequence,
title, publisher and claim signatures. Same URL + same digest is DUPLICATE;
same URL + changed digest is UPDATE; a new URL is NEW. The index is rebuildable
and is not the Core journal. Core remains authoritative for semantic state.
Claim signatures are deterministic over proposition, polarity, topic,
conditions, temporal state and modality.

## 4. Provenance and attribution

Every document creates a publisher Entity and Evidence record. Evidence records
canonical URL, title, publisher, retrieval timestamp, source digest, capture
status and byte range. Quoted speech must be represented separately from the
publisher's own assertion. Publisher identity is never treated as the author
of quoted content by default. Attribution candidates require an explicit
speaker/source entity and a quotation ScopeSpan.

## 5. Long-document chunking and ScopeSpan

Text is NFC-normalized once. Chunk boundaries are sentence/newline-aware and
overlap to preserve cross-boundary context. Every chunk retains half-open
UTF-8 byte offsets into the immutable whole-document snapshot. Scope anchors
are converted back to whole-document offsets and validated by the C++ Core.
The initial extractor supports NEGATION, MODALITY, TEMPORAL and QUOTATION
ScopeSpan. CONDITION requires two Claim targets under the current Core
validator and is therefore retained in the Claim applicability metadata until
a valid antecedent/target pair exists.

## 6. Runtime API

`WebDocumentRecognitionEngine.prepare(text, url, publisher, ...)` produces a
packet and SemanticDelta without committing. `ingest(..., core)` performs
atomic Packet + SemanticDelta admission and only then updates the rebuildable
index. Repeated identical URL/content is skipped. Provider output remains
subject to the existing strict LLM output validator and RecognitionScoring.

## 7. Known limits

- Current provider contract emits one claim candidate per chunk. Multi-claim
  extraction requires a versioned provider response schema.
- Regex marker discovery is a candidate generator, not a semantic truth oracle.
  False marker hits must be caught by conformance tests and provider review.
- Geography and exact valid-time intervals are represented but are not yet
  reliably inferred by the v0.1 heuristic layer.
- Cross-document contradiction candidates require persisted Claim records;
  the comparator itself is deterministic and side-effect-free.
- The index and Core journal are separate durability domains. A failed index
  write after Core commit is recovered by replay/reconciliation, not rollback.
- A Web corpus case is not counted as an executed pass unless its source text,
  expected output and actual runner result are all present.
