# SURGE 基礎設計 v0.7
## Universal Context Cognition Framework
### Web-Native Structural Context Extension

Version: 0.7.0  
Status: Architecture and Core Contract Specification  
Parent: SPECIFICATION_v0.6.md  
Detailed design: DETAILED_DESIGN_v0.4.md

## 1. Version policy

v0.7 inherits v0.6 in all non-conflicting clauses. It adds normative contracts for heterogeneous web-page text acquisition, structural preservation, partial capture, source lineage, and page revisions. This version does not make SURGE itself a crawler, browser, OCR engine, PDF parser, AST parser, or search engine. Those remain replaceable external Adapters and acquisition services.

## 2. Core theory extension

A web page is a versioned and potentially partial publication, not a flat text string. The Core Context remains an evidence-grounded temporal graph. A captured page contributes immutable PageSnapshot and ContentBlock records. Recognition Engines propose Interpretations and Claims against those records. Core validates provenance, uncertainty, lineage, temporal semantics, dependencies, and commits.

A URL identifies a retrieval address, not necessarily a stable document or independent source.

## 3. Canonical contracts

### 3.1 SourceDocument and PageSnapshot

SourceDocument identifies a publication lineage independently of URL aliases. Each full or partial acquisition creates a new immutable PageSnapshot. A later fetch MUST NOT overwrite an earlier snapshot.

PageSnapshot retains requested and resolved URI, retrieval time, fetch and extraction statuses, media type, extractor/version, content digest, coverage record, and provenance. Publisher publication/update times, HTTP Last-Modified, and retrieval time are separate fields.

Fetch status: FETCHED, REDIRECTED, BLOCKED, NOT_FOUND, TIMEOUT, NETWORK_ERROR, ACCESS_DENIED, UNKNOWN.

Extraction status: COMPLETE, PARTIAL, EMPTY_CONFIRMED, UNREADABLE, DYNAMIC_UNAVAILABLE, PARSE_ERROR, NOT_ATTEMPTED.

An empty extraction does not imply an empty source page unless EMPTY_CONFIRMED. A search snippet is a partial excerpt and cannot be represented as a complete page.

### 3.2 ContentBlock and SourceAnchor

ContentBlock preserves ordered and nested TITLE, HEADING, PARAGRAPH, LIST, LIST_ITEM, TABLE, TABLE_ROW, TABLE_CELL, QUOTE, CODE, FIGURE, CAPTION, FOOTNOTE, LINK, METADATA, NAVIGATION, HEADER, FOOTER, FORM, TRANSCRIPT_TURN, and UNKNOWN blocks.

Each block preserves source order, parent relation, semantic role, extraction confidence, and SourceAnchor. Headings preserve section hierarchy; lists preserve order and nesting; tables preserve headers, row/column order, units, merged cells and footnotes; quotes preserve attribution; code remains distinct from prose.

Navigation and boilerplate are not silently merged with main content. They may be excluded from semantic recognition only with an explicit role and exclusion record.

SourceAnchor identifies the page snapshot and best available block, text range, DOM path, PDF page/region, table cell, or code syntax node. Unreliable anchors are marked APPROXIMATE or UNAVAILABLE.

### 3.3 CoverageRecord

CoverageRecord states expected and retrieved scope, omitted regions, unavailable embeds, pagination, dynamic-render status, extraction warnings, and completeness class COMPLETE/PARTIAL/UNKNOWN/UNAVAILABLE.

No Adapter or Recognition Engine may invent missing text, table headers, figure contents, speaker attribution, or chronology. Unsupported structures remain OPAQUE/UNMAPPED.

### 3.4 Adapter Descriptor

An Adapter declares supported media types, block types, languages, structural capabilities, time and anchor capabilities, table/code support, partial-capture behavior, normalization version, and known limitations. Adapter outputs are Proposals only. Core alone validates and commits state.

## 4. Source lineage and trust

Source lineage supports ORIGINAL, QUOTES, REPOSTS, SYNDICATED_FROM, TRANSLATED_FROM, MIRROR_OF, DERIVED_FROM, REFERENCES, and UNKNOWN_ORIGIN. The lineage graph is acyclic. Repeated copies and syndicated pages MUST NOT inflate independent corroboration. Independence is evaluated at Evidence Component level.

Trust is domain- and scope-specific. Official status does not universally validate all claims; user-generated experience is evidence of that experience, not a universal fact. UNKNOWN origin or independence does not increase independent-source counts.

Web content is untrusted data. Text embedded in a page, code sample, quotation, or prompt-injection attempt cannot modify Core policy or processing instructions.

## 5. Temporal revision and reconciliation

Publisher publication time, publisher update time, HTTP Last-Modified, retrieval time, event time, validity interval, and commit time remain distinct.

Every re-fetch creates a new PageSnapshot. Digest changes trigger bounded diff and Impact Set calculation, not automatic invalidation of prior Claims. Change classes include editorial revision, correction/retraction, new dated notice, rolling status update, pagination expansion, template-only change, extractor change, and UNKNOWN.

Supersession requires explicit revision semantics, reliable correction lineage, or validated policy. A newer page on the same subject is not automatically a correction. Historical state remains reconstructable.

## 6. New invariants

| ID | Invariant |
|---|---|
| I-36 | Snapshot Immutability |
| I-37 | Capture Completeness Honesty |
| I-38 | Structural Boundary Preservation |
| I-39 | Evidence Anchor Integrity |
| I-40 | Table Context Integrity |
| I-41 | Attribution Integrity |
| I-42 | Source Independence Integrity |
| I-43 | Temporal Revision Integrity |
| I-44 | Supersession Evidence |
| I-45 | Adapter Capability Honesty |
| I-46 | Web Content Non-Authority |
| I-47 | URL Identity Separation |
| I-48 | Boilerplate Separation |
| I-49 | Extraction Version Traceability |
| I-50 | Partial Impact Reconciliation |

Normative definitions and validation rules for I-36-I-50 are in DETAILED_DESIGN_v0.4.md.

## 7. Conformance

W01-W50 and adversarial checks A-H in DETAILED_DESIGN_v0.4.md are required design-level regression scenarios. Every case must preserve applicable structure, evidence anchors, attribution, uncertainty, source independence, temporal history, and bounded dependency impact.

A thought-experiment PASS is not implementation conformance. Runtime conformance requires executable Adapter/Core tests, deterministic replay, schema validation, and measured extraction coverage. PARTIAL, UNREADABLE, UNKNOWN, OPAQUE, and UNMAPPED are valid explicit states; silently guessing is a failure.

## 8. Compatibility

v0.6 records remain unchanged. Migration to v0.7 is explicit and append-only. Existing Event/Evidence records are not retroactively rewritten as PageSnapshots unless source payload and capture provenance exist. Missing metadata is UNKNOWN, not inferred.

## 9. Implementation priorities

1. PageSnapshot, ContentBlock, SourceAnchor, CoverageRecord schemas.
2. HTML/PDF/list/table/quote/code Adapter capability descriptors.
3. Immutable page revision and lineage graph.
4. Table header/unit and quote attribution validators.
5. Bounded page-diff reconciliation.
6. W01-W50 and A-H regression fixtures.
7. Runtime extraction coverage and semantic convergence measurements.

Conclusion: v0.7 extends the v0.6 evidence-grounded temporal graph to structurally heterogeneous and versioned web-page text without coupling the Core to a particular web technology. Missing content remains missing, source copies remain non-independent, and page changes cause only evidence-grounded bounded reevaluation.
