# ScopeSpan ContextGraph Integration v0.1

## Purpose

ScopeSpan is now a first-class typed record in ContextGraph, not merely an
independent validator input. It preserves the linguistic/structural scope that
qualifies Claims and Evidence across an immutable source snapshot.

## Typed record

Each ScopeSpan stores:
- stable span ID and ScopeKind;
- SHA-256 digest and UTF8_BYTE_NFC offset basis;
- exact opener anchor and optional closer anchor;
- target Claim references and antecedent Entity candidates;
- attribution, modality, temporal role and structural parent references;
- packet references that establish source provenance.

Anchors use half-open UTF-8 byte offsets. The Core validates the source as UTF-8,
recomputes SHA-256 over the exact normalized source bytes, verifies that every
anchor's digest and offset basis match the span, and compares the exact anchor
text against the referenced byte range.

## Core transaction

A SemanticDelta may contain nodes, relations, ScopeSpan proposals and focus
updates. ContextGraph stages the entire delta in memory, validates graph
references and all ScopeSpan proposals, and only then commits its journal
transaction. A rejected ScopeSpan therefore leaves no partial graph mutation.

ScopeSpan records are written as SCOPE_SPAN_ADD journal events. On restart,
ContextGraph decodes each record and re-runs the validator against the retained
immutable source snapshot and replayed graph nodes. The resulting status and
effect are stored in the graph record:
- CLOSED or UNRESOLVED;
- the corresponding ScopeEffect;
- whether an unconditional Claim may be emitted.

Unresolved scopes remain durable records. They are not silently promoted to
closed scopes or converted into unconditional Claims.

## Interfaces

- C++: SemanticDelta.scope_spans / ContextGraph.scope_spans()
- C ABI: surge_context_graph_apply_scoped
- Python: SemanticCoreBridge.apply_semantic_delta with scope_spans and
  source_snapshot fields

The C ABI limits a single delta to 4,096 scope spans, 1,024 target/antecedent
references per span, and 4,096 packet references per span.

## Privacy and storage

The immutable source snapshot is retained in the journal scope event so exact
anchors can be revalidated during replay. This is an intentional durability
trade-off: applications should apply their normal retention and access controls
to the EventJournal. Future storage compaction may replace repeated snapshots
with durable source references, provided those references remain immutable and
resolvable.

## Current boundary

This stage makes ScopeSpan typed, validated, durable and replayable. It does
not yet make Packet Admission and SemanticDelta a single journal transaction.
They remain two transactions; atomic packet-plus-semantic commit and recovery
are the next implementation stage. It also does not claim automatic high-
accuracy Japanese scope extraction from a live LLM; the LLM must propose exact
anchors and the Core remains authoritative.
