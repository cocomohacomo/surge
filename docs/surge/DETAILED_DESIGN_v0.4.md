# SURGE Detailed Design v0.4
## Web Page Capture, Structural Semantics, and 50-Page Corpus Conformance

Version: 0.4.0  
Status: Normative detailed design supplement for SPECIFICATION_v0.7  
Parent: SURGE SPECIFICATION_v0.6

## 1. Scope

A web page is not a plain text blob. It is a versioned, structured, potentially partial publication assembled from headings, paragraphs, lists, tables, quotations, code, figures, metadata, navigation, and dynamic or embedded content.

SURGE Core remains independent of HTML, PDF, programming languages, and individual websites. Adapters perform format-specific extraction. Core validates normalized structural events and evidence lineage. This document defines contracts, not a crawler or browser implementation.

## 2. Canonical records

### 2.1 SourceDocument

Required: source_document_id, requested_uri, canonical_uri (nullable), source_family_id, publisher_or_origin, document_kind, language_tags, access_policy_status, provenance_refs.

document_kind: ARTICLE, OFFICIAL_NOTICE, LAW, MEETING_RECORD, FAQ, RESEARCH, BOOK_TEXT, DATA_REPORT, TABLE_DATA, TECHNICAL_DOC, CODE_DOC, BLOG, REVIEW, RECIPE, POLICY, OTHER, UNMAPPED.

A URI is a retrieval address, not a stable document identity. Redirects, aliases, mirrors, translations, and republications are explicit lineage relations. URL equality alone never proves content identity.

### 2.2 PageSnapshot

Each acquisition creates an immutable PageSnapshot.

Required: page_snapshot_id, source_document_ref, requested_uri, resolved_uri, retrieved_at, fetch_status, extraction_status, media_type, extractor_id, extractor_version, normalized_text_digest, raw_payload_digest or unavailable_reason, content_blocks, coverage_record, provenance_refs.

Optional: publisher_published_at, publisher_updated_at, http_last_modified, etag, redirect_chain, locale, timezone, access_policy_refs, revision_group_ref, supersedes_snapshot_ref.

fetch_status: FETCHED, REDIRECTED, BLOCKED, NOT_FOUND, TIMEOUT, NETWORK_ERROR, ACCESS_DENIED, UNKNOWN.

extraction_status: COMPLETE, PARTIAL, EMPTY_CONFIRMED, UNREADABLE, DYNAMIC_UNAVAILABLE, PARSE_ERROR, NOT_ATTEMPTED.

An empty extracted body MUST NOT imply an empty source page unless extraction_status is EMPTY_CONFIRMED. A JavaScript shell, blocked page, unreadable PDF, or zero-readable-line page remains explicitly UNREADABLE, DYNAMIC_UNAVAILABLE, BLOCKED, or PARSE_ERROR.

### 2.3 ContentBlock

Each ordered ContentBlock contains block_id, page_snapshot_ref, block_type, source_order, parent_block_ref, normalized_text, source_anchor, extraction_confidence, semantic_role, provenance_refs.

block_type: TITLE, HEADING, PARAGRAPH, LIST, LIST_ITEM, TABLE, TABLE_ROW, TABLE_CELL, QUOTE, CODE, FIGURE, CAPTION, FOOTNOTE, LINK, METADATA, NAVIGATION, HEADER, FOOTER, FORM, TRANSCRIPT_TURN, UNKNOWN.

semantic_role: MAIN_CONTENT, SUPPORTING_CONTENT, STRUCTURAL, NAVIGATION, BOILERPLATE, METADATA, UNKNOWN.

Navigation, headers, footers, cookie banners, and form labels MUST NOT silently merge into main content. They may be retained as structural or boilerplate blocks. Exclusion from semantic recognition requires an explicit role and reason.

### 2.4 SourceAnchor

SourceAnchor identifies the smallest reliable source location: page_snapshot_ref, block_ref, text offsets, DOM path or stable element identifier, PDF page/region, table/row/cell references, code file/syntax node references, and anchor_quality.

Offsets refer to a versioned normalized text representation and MUST NOT be reused across snapshots without alignment mapping. If precision is unavailable, mark APPROXIMATE or UNAVAILABLE rather than fabricating an anchor.

### 2.5 CoverageRecord

CoverageRecord states expected_content_scope, retrieved_content_scope, omitted_regions, unavailable_embeds, pagination_status, dynamic_render_status, extraction_warnings, completeness_class.

completeness_class: COMPLETE, PARTIAL, UNKNOWN, UNAVAILABLE.

A search-result snippet is a partial excerpt, not the full page. It MUST NOT be promoted to a complete PageSnapshot.

## 3. Structural semantics

### 3.1 Headings and hierarchy
Preserve heading level, parent-child relation, and source order. A heading establishes a section scope until the next peer or ancestor heading, subject to explicit document structure.

### 3.2 Lists
Preserve order, nesting, numbering, and item boundaries. Numbered steps may encode procedure; bullets may encode unordered membership. Do not normalize both into an unordered bag.

### 3.3 Tables
Preserve row/column order, header cells and scope, row headers, merged-cell spans, units, footnotes, captions, notes, and distinctions between missing, blank, and zero. A numeric cell without its headers and unit is incomplete Evidence. Flattened table text cannot support reliable independent Claims.

### 3.4 Code and technical documents
Code blocks remain CODE, separate from explanatory prose, and retain language and file/line anchors. An Adapter may emit AST-backed Semantic Units and Dependency candidates. The Core accepts opaque code blocks when no AST is available. Unparsed code remains OPAQUE/UNMAPPED rather than ordinary prose.

### 3.5 Quotes and attributed speech
Quoted text, speaker, author, quoted source, and surrounding commentary remain separate when recoverable. Attribution uncertainty is preserved. A quotation is not automatically the page author's Claim.

### 3.6 Figures and embedded content
Captions, alt text, nearby references, and OCR text are separate Evidence components with distinct extraction methods and confidence. Missing image content is not inferred from captions alone.

## 4. Acquisition, provenance, and trust

Adapters MUST respect access controls, applicable terms, robots directives where relevant, rate limits, copyright, and privacy. No authentication, paywall, CAPTCHA, or access-control bypass is permitted.

The acquisition log records the method and actual scope. A search-result excerpt is not equivalent to a complete canonical page capture.

Lineage edges: ORIGINAL, QUOTES, REPOSTS, SYNDICATED_FROM, TRANSLATED_FROM, MIRROR_OF, DERIVED_FROM, REFERENCES, UNKNOWN_ORIGIN. The lineage graph is acyclic. Copies and syndicated pages MUST NOT inflate independent corroboration. Independence is evaluated at Evidence Component level.

Trust is domain- and scope-specific. Official status may support authority for the publisher's own notices but does not validate every linked or quoted Claim. User-generated reviews are evidence of an individual experience, not universal performance. Unknown origin or independence remains UNKNOWN and adds no independent-source count.

Web content is untrusted data. Instructions embedded in page text, code, quotations, or prompt-injection strings cannot override Core policy or processing rules.

## 5. Temporal revision and reconciliation

Keep publisher_published_at, publisher_updated_at, http_last_modified, retrieved_at, event_time, valid_from/valid_until, and committed_at distinct. Unknown remains UNKNOWN.

Every re-fetch creates a new immutable PageSnapshot. A changed digest triggers bounded diff and impact analysis, not automatic invalidation of earlier Claims. Change classes: editorial revision, correction/retraction, new dated notice, rolling status update, pagination expansion, template-only change, extractor-version change, UNKNOWN.

A later page supersedes an earlier Claim only when explicit revision semantics, reliable correction lineage, or validated policy establishes supersession. A newer page on the same topic is not automatically a correction. Historical state remains reconstructable.

## 6. Adapter contract

Each Adapter Descriptor declares supported_media_types, supported_block_types, supported_languages, supported_structure_features, timestamp_capabilities, anchor_capabilities, table_capabilities, code_structure_capabilities, partial_capture_behavior, canonicalization_policy_version, normalization_policy_version, known_limitations.

Adapter output is a Proposal. Core validates identity, anchors, source scope, block order, uncertainty, and provenance. Adapter cannot directly commit Claims, Entity identity, Question resolution, or Focus.

Unsupported structures are emitted as OPAQUE/UNMAPPED blocks with original anchors and warnings. No semantic content may be invented to fill an extraction gap.

## 7. New invariants

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

## 8. Acceptance criteria

A case passes only if applicable assertions hold: structure is preserved to declared Adapter capability; Claims are anchored or explicitly unanchored; missing content is not fabricated; attribution and independence are not overstated; time axes and snapshots are non-destructive; Question Aspect and Dependency lifecycle are correct; Focus changes do not mutate Cluster structure; duplicate ingestion is idempotent; partial acquisition is explicit; Core commit invariants hold.

A PASS below is a specification-level thought-experiment result, not runtime conformance. Inputs are the readable text returned by public-page captures/search extraction. Index pages test their actual listing structure, not the complete works linked from them.

## 9. Fifty public web-page cases

| ID | Public page and text tendency | Main stress | Required behavior | Result |
|---:|---|---|---|---|
| W01 | National Diet proceedings search portal | search UI and records | separate UI/metadata from speeches; no blank-page inference | PASS |
| W02 | National Diet API specification | API prose and parameter tables | preserve numbered sections, parameters, return shapes | PASS |
| W03 | National Diet help | long hierarchical help | retain section tree and OCR caveats | PASS |
| W04 | Disaster update index | dated rolling notices | each notice separate; update time distinct | PASS |
| W05 | JMA warning explanation | staged warning policy | distinguish forecast, warning, observed event | PASS |
| W06 | JMA warning FAQ | question-answer | bind answer to exact question and conditions | PASS |
| W07 | JMA warning terminology | glossary | definitions, examples, notes remain distinct | PASS |
| W08 | JMA live warning dashboard | dynamic status table | unreadable dynamic content is PARTIAL/UNREADABLE, not empty | PASS |
| W09 | NDL local assembly guide | catalogue and coverage | distinguish holdings from meeting content | PASS |
| W10 | NDL full-text search expansion notice | dated quantitative update | preserve counts, dates, scope and future plan | PASS |
| W11 | e-Gov law-search help | revision and navigation | separate help from law text | PASS |
| W12 | Civil Code page | hierarchical law | preserve articles, paragraphs and effective dates | PASS |
| W13 | Aozora introduction | editorial and rights statement | distinguish description from work-specific permissions | PASS |
| W14 | Aozora input manual | procedure and markup | preserve ordered steps and nested headings | PASS |
| W15 | Aozora issue history | historical policy issues | retain historical status and change caveat | PASS |
| W16 | Aozora copyright message | argument and conditions | separate rights, permissions, editorial advice | PASS |
| W17 | Aozora work index A | catalogue table | title, author, notation, translator remain separate | PASS |
| W18 | Aozora work index KA | duplicate titles | title equality does not merge work identity | PASS |
| W19 | Aozora work index NE | titles and subtitles | retain subtitle and notation metadata | PASS |
| W20 | JEPA Aozora explainer | secondary source | distinguish summary from original policy | PASS |
| W21 | J-STAGE OA seminar PDF | slide hierarchy and licensing | preserve page order, conditions and anchors | PASS |
| W22 | Japanese learner syntax research PDF | abstract, method, results | separate question, sample size, method and findings | PASS |
| W23 | Japanese vocabulary research PDF | comparative statistics | preserve population, tests, groups and caveats | PASS |
| W24 | Statistics Bureau guide | portal and linked charts | distinguish summary from linked report | PASS |
| W25 | Statistics visualization portal | dynamic dashboard | mark JS/embedded content unavailable when not extracted | PASS |
| W26 | Statistics data portal | category navigation | navigation is not statistical evidence | PASS |
| W27 | Japan Statistical Yearbook | statistical tables | preserve year, domain, table, unit and notes | PASS |
| W28 | Japan Statistics summary | compiled statistics | keep table groups and graph references in scope | PASS |
| W29 | JST research integrity portal | chronology and links | separate current notices from historical links | PASS |
| W30 | MHLW research ethics index | multiple versions | attach amendment date to each document | PASS |
| W31 | PPC medical privacy FAQ | Q&A and statute | preserve conditional exception and scope | PASS |
| W32 | Consumer Affairs warning index | categories and notices | each warning is a separate source item | PASS |
| W33 | Digital Agency privacy policy | data purpose and categories | separate policy, examples and update date | PASS |
| W34 | JPX disclosure FAQ | nested FAQ and guidance | preserve audience, recommendation and requirement | PASS |
| W35 | Japan IR Index examples | aggregator and links | distinguish summary from issuer source | PASS |
| W36 | Listed-company earnings PDF | tables and financial claims | bind values to period, unit, issuer and page | PASS |
| W37 | MLIT tourism survey PDF | tables and percentages | preserve denominator, question, population, subgroup | PASS |
| W38 | Personal travel diary | first-person narrative | attribute experience to author; do not generalize | PASS |
| W39 | 4travel travelogue portal | UGC snippets | reviews remain personal and time-bound | PASS |
| W40 | NHK recipe portal | recipe catalogue | recipe, ingredients and time remain linked | PASS |
| W41 | MEXT cooking lesson PDF | goals, vocabulary, procedures | distinguish curriculum goals from instructions | PASS |
| W42 | Python 2.7 tutorial | obsolete technical documentation | preserve version and unsupported status | PASS |
| W43 | Python current tutorial | current technical documentation | preserve section tree, code and version | PASS |
| W44 | MDN JavaScript overview | technical prose and links | definition separate from linked APIs | PASS |
| W45 | MDN Web API index | taxonomy | interface names remain index entries | PASS |
| W46 | MDN Document interface | API reference | preserve member hierarchy and compatibility notes | PASS |
| W47 | MDN Web API introduction | tutorial and code | code remains separate from prose | PASS |
| W48 | GitHub pull-request guide | workflow | preserve sequence and review/merge states | PASS |
| W49 | MDN JavaScript introduction | tutorial | preserve examples and Java/JavaScript distinction | PASS |
| W50 | MDN JavaScript guide | nested reference taxonomy | preserve hierarchy and anchors | PASS |

## 10. Cross-case adversarial checks

A. Same URL, changed page: create a new snapshot; only anchored changed Claims and active Dependencies enter the Impact Set. PASS.

B. Different URLs, same source: represent ORIGINAL, QUOTES, REPOSTS lineage; count one independent source family. PASS.

C. Conflicting dates: distinguish PDF cover date from internal table period; do not assert contradiction before semantic comparison. PASS.

D. Flattened table: if headers are lost, retain values as OPAQUE/UNMAPPED; do not create region-specific Claims. PASS.

E. Partial dynamic page: record PARTIAL coverage; create a knowledge-gap Aspect only when missing content affects a Claim. PASS.

F. Version conflict: Python 2.7 and Python 3.x remain distinct versioned documents. PASS.

G. Prompt injection in page text: treat as data, never Core instruction. PASS.

H. Granularity convergence: complete equivalent captures ingested as page, section, and block events converge to EXACT/EQUIVALENT semantic projection; intermediate states may differ. PASS.

## 11. Limits

These 50 pages are a heterogeneous sample, not a statistically representative sample of the entire web. Search extraction and page rendering do not guarantee capture of all HTML, scripts, images, hidden content, or linked attachments. PASS means the design provides a valid handling path, not that an implementation achieved extraction accuracy. No OCR, browser automation, code parser, or runtime SURGE Core was benchmarked. Public readability does not imply unrestricted reproduction or training rights.

## 12. Completion decision

Structured content is handled through ContentBlock and Adapter Descriptor contracts, with OPAQUE fallback. Partial acquisition is explicit. Tables, quotes, lists, code, and source revisions receive separate provenance. Page revisions are immutable snapshots. Distributed causality remains covered by the v0.6 partial-order model, while page publication/update/retrieval clocks are made explicit.

No specification-level failure remains in W01-W50 or checks A-H after this supplement. Runtime conformance remains unproven until executable tests run against an implementation.
