# SURGE Language Runtime and Replaceable Language Packs v0.1

Status: normative architecture proposal for implementation
Version: 0.1.0
Target: SURGE Core 0.9.x and later

## 1. Purpose

SURGE owns the complete language-recognition boundary. Applications MUST NOT implement a parallel Language Adapter, morphology engine, lexicon, n-gram scorer, orthographic normalizer, or language-specific candidate-selection subsystem.

A SURGE distribution consists of:
- SURGE Core: language-neutral streaming cognition, evidence/claim lifecycle, candidate decision, context graph, Question/Interest/Focus, Dependency, Chaos Cluster, temporal reassessment, persistence, retention and resource governance.
- Language Runtime: generic execution contract and loader for language capabilities.
- Language Set: replaceable, versioned bundle containing all language-dependent data, algorithms, policies and optional native modules.
- Host bindings: thin C/C++/Python/application APIs. They transport input and output and do not perform language recognition.

The selected Language Set is loaded by SURGE itself. Switching language means replacing the selected Language Set, or selecting another installed set, without changing application code or introducing application-owned language logic.

## 2. Ownership boundary

| Capability | Owner |
|---|---|
| UTF-8 validation, stream/event sequencing, byte/codepoint anchors | SURGE Core |
| Generic candidate/evidence/claim contracts and ranking orchestration | SURGE Core |
| Lexicon lookup and candidate generation | Language Set |
| Morphology, segmentation, POS inventory and connection model | Language Set |
| Character, syllable, phoneme, transliteration and reading conversion | Language Set |
| Word/POS/character n-gram models and language model weights | Language Set |
| Semantic/syntactic feature extraction and language-specific relation mapping | Language Set |
| Inflection, agreement, compounding and grammatical constraints | Language Set |
| Orthographic conversion, script selection, punctuation and number formatting | Language Set |
| Locale-specific date/time, unit, currency and numeric parsing | Language Set |
| Evidence provenance, deduplication, temporal validity and conflict handling | SURGE Core |
| Question / Interest / Focus routing and cross-cluster relations | SURGE Core |
| Final candidate decision, defer/revise/commit authority | SURGE Core |
| Memory budgets, forgetting, snapshots, journal and replay | SURGE Core |
| C/Python APIs and application-specific I/O | Host bindings |

A Language Set may contribute observations, hypotheses, constraints and calibrated feature values. It MUST NOT own the authoritative context graph, journal, Question/Focus lifecycle, cross-language decision policy, or final commit journal.

## 3. Runtime architecture

Required execution path:

Input Stream -> SURGE Stream Ingress -> Language Runtime -> Candidate/Evidence Packet -> SURGE Recognition Core -> Decision/Question/Focus/Reassessment -> Language Renderer -> Commit/Revision

The Language Runtime executes bounded, language-specific stages:
1. input normalization and script/encoding profile;
2. tokenization/segmentation and morphological lattice generation;
3. lexicon and user-dictionary lookup;
4. phonetic/reading conversion and transliteration;
5. grammar, morphology and connection constraints;
6. word/POS/character n-gram feature production;
7. optional syntax and semantic frame extraction;
8. numeric, date/time, unit and named-entity recognition;
9. orthographic rendering candidates and style constraints.

Stages are capability-declared and may be absent. Absence is represented explicitly; the runtime MUST NOT fabricate evidence. SURGE Core receives typed observations and remains responsible for integrating evidence across time and clusters.

## 4. Replaceable Language Set package

A Language Set is a self-describing package with:
- manifest.json validated against schemas/surge-language-pack-v1.0.schema.json;
- stable language identifier (BCP-47 where applicable) and script/locale metadata;
- package format and SURGE Language Runtime ABI compatibility range;
- declared capabilities and required/optional stages;
- resource ceilings and declared memory profile;
- lexicon, morphology, connection, n-gram and correction resources;
- orthography, punctuation, numeral and locale rules;
- optional compiled module implementing the stable Language Runtime ABI;
- per-resource cryptographic digest, provenance, license and version;
- deterministic test vectors and quality profile.

The package is immutable after validation. Updates create a new package version. The loader validates the complete package before activation and atomically swaps the active Language Set at a stream/session boundary. A failed load leaves the previous set active.

A session pins the Language Set ID, version, content digest and ABI version at creation. A running session MUST NOT silently switch language resources mid-stream. Explicit migration creates a new session or a journaled language-set transition with a declared reset/reassessment policy.

## 5. Runtime ABI

The generic Language Runtime ABI MUST expose these logical operations:
- describe_capabilities()
- create_session(config, resource_budget)
- push_input(event, session)
- generate_candidates(span, session, bounded_output)
- produce_evidence(candidate_set, context_snapshot, session)
- extract_relations(candidate_set, session) (optional)
- render_candidates(decision_set, render_policy, session)
- reset_session(session)
- destroy_session(session)

The ABI uses versioned, size-tagged structures. All buffers are caller-bounded or runtime-owned with explicit release functions. No operation may allocate without respecting the supplied resource budget. No operation may perform network access. The Core owns time, event ordering, durable identity, and commit.

The native module ABI is optional. Data-only packs are valid when the generic built-in runtime supports their declared resources. A native module MUST be sandboxable by process boundary where the host requires it; the default embedded profile MUST support in-process offline execution.

## 6. Evidence contract

Every language-produced observation MUST include:
- stable producer ID and producer version;
- Language Set ID/version/digest;
- source event ID and exact source span;
- candidate ID and candidate span;
- evidence kind and typed payload;
- raw feature value and calibrated value, where applicable;
- confidence/uncertainty and calibration profile;
- observation time and validity interval when known;
- provenance/license metadata where required.

Evidence is immutable. Duplicate producer observations are idempotent by stable evidence identity. Missing or expired evidence is not converted to a zero-valued positive or negative assertion. SURGE may down-weight, invalidate, forget or request reassessment according to policy while preserving audit history within retention limits.

## 7. Decision and uncertainty

Language Sets generate candidates and language evidence; SURGE Core decides. The Core MUST compare candidates using evidence provenance and calibrated feature groups, avoid double-counting correlated evidence from the same producer family, apply temporal validity and dependency changes, route contradictions into Questions and Interest/Focus, preserve unresolved alternatives in Chaos Cluster, return SELECT/DEFER/REJECT/REASSESS with traceable reasons, and commit only through the Core journal.

A Language Set may provide a calibrated scoring profile, but cannot bypass Core decision authority. Ties and budget exhaustion have deterministic, explicitly configured outcomes.

## 8. Language switching and multi-language operation

The runtime supports:
- one active Language Set per session by default;
- explicit selection by language ID, locale, script or caller policy;
- multiple installed Language Sets with bounded resident-resource policy;
- optional parallel candidate generation from multiple Language Sets;
- cross-language candidate comparison only through shared Core evidence/decision contracts.

Automatic language identification is a Language Set capability or a separate declared detector pack. It may propose a set, but activation requires explicit policy and a safe session boundary. Unknown or mixed-language spans may be routed to multiple sets and retained as unresolved hypotheses.

Switching sets MUST NOT erase existing journal history. Existing claims retain the Language Set provenance under which they were generated. Reassessment under a new set creates new evidence and revisions; it does not rewrite historical evidence.

## 9. Resource and safety requirements

Every package declares hard maxima for package bytes, unpacked bytes, resident memory, temporary working memory, maximum input span and candidate count, maximum lattice nodes/arcs, n-gram lookup work and cache bytes, native module stack/heap budget, output bytes and per-event execution budget.

The loader rejects packages whose declared or measured resource profile exceeds host policy. Runtime operations fail closed on budget exhaustion and MUST NOT partially commit a candidate packet. All parsing is bounds-checked; malformed package data cannot cause unbounded allocation or recursion.

The baseline profile is offline, CPU-only, deterministic and suitable for constrained edge PCs. SIMD or accelerator backends are optional and MUST have a deterministic CPU fallback.

## 10. Migration from application-owned adapters

Applications MUST remove language-specific recognition logic from their own runtime once the equivalent capability is available in a Language Set. During migration, compatibility wrappers may delegate to SURGE Language Runtime, but they MUST NOT maintain independent dictionaries, morphology state, n-gram state, language-specific ranking, or authoritative language state.

For kana-decoder specifically, the current surge_language.*, morphology decoder, lexicon trie, n-gram scorer, correction rules, connection model, pack loader and Japanese rendering logic are migration sources. They are to be moved into a SURGE Japanese Language Set/runtime implementation. kana-decoder will retain only kana input/output transport, public compatibility APIs, test/evaluation harnesses and application packaging.

The old packs/ja-* format is a migration input only. A converter may produce the SURGE Language Set package; the old format must not remain a second production runtime.

## 11. Conformance requirements

A conforming implementation MUST test:
1. load and validate a data-only Language Set;
2. load and validate a native-module Language Set;
3. reject ABI-incompatible, digest-invalid and capability-inconsistent packages;
4. preserve the previous active set after failed activation;
5. deterministic language switching at session boundaries;
6. session pinning and journal provenance across restart;
7. Japanese pack replacement without rebuilding the host application;
8. a second structurally different language pack using the same Core API;
9. no language-specific code in host-side decode path;
10. candidate ranking, contradiction, Question/Focus, defer and Chaos retention through Core;
11. duplicate evidence idempotency and correlated-evidence non-double-counting;
12. hard resource-cap rejection without partial journal mutation;
13. reproducible CPU-only execution and bounded memory/latency.

Schema validation alone is not full runtime conformance. Quality, memory and latency must be reported per Language Set and host profile.

## 12. Implementation stages

- L1 Contract: schema, ABI headers, loader contract, package validation and conformance fixtures.
- L2 Generic runtime: bounded data resource readers, generic lexicon/morphology/connection/n-gram interfaces and evidence producers.
- L3 Japanese Language Set: migrate the existing kana-decoder Japanese resources and algorithms into SURGE.
- L4 Core decision integration: candidate packet admission and Core-owned ranking/commit; remove paths.front() authority.
- L5 Host slimming: make kana-decoder a thin host binding and compatibility facade.
- L6 Cross-language proof: add a second language set and verify that no Core or host rebuild is required for data-only replacement.
- L7 Performance gates: compare CER, exact sentence, RTF, peak RSS, package size, load time and switch/reassessment cost.

No stage is complete merely because its specification or schema exists. Each stage requires implementation and executable conformance tests.


## 13. Normalization Runtime (normative)

Normalization is part of the SURGE language runtime, not an application-side postprocessor. The term covers both linguistic normalization and final surface realization.

The SURGE normalization path is:
1. ingest the raw input and preserve its immutable source span;
2. produce language-specific reading, morphology, semantic and syntactic hypotheses;
3. create typed normalization hypotheses (kanji/kana/katakana/Latin/digits/symbols/punctuation, spacing, inflection and orthographic variants);
4. attach evidence and constraints to each hypothesis, including source span, language-set provenance, style/locale policy and temporal validity;
5. let SURGE Core compare, defer, revise or select hypotheses using context, dependencies, Questions/Interest/Focus and retained alternatives;
6. pass the selected semantic/lexical decision to the Language Set renderer for deterministic surface realization;
7. validate the realized output against Core decision spans and commit the normalized result through the SURGE EventJournal.

The Language Set owns language-specific normalization resources and algorithms: script conversion, orthographic variants, okurigana, inflection realization, punctuation, spacing, numeral/date/time/unit/currency formatting, transliteration, locale rules and rendering templates. It may return multiple surface hypotheses with typed constraints and calibrated evidence.

SURGE Core owns the generic NormalizationHypothesis/NormalizationDecision lifecycle, provenance, candidate comparison, ambiguity handling, revision, span alignment, budget enforcement and commit authority. The renderer MUST NOT silently change semantic identity or choose a different lexical claim. If realization exposes a conflict, it returns a typed rendering conflict to Core for reassessment.

Host applications MUST NOT contain a second normalization adapter, postprocessor, Japanese conversion table, punctuation engine, numeral formatter or final surface selector. Host code may perform transport encoding and buffer copying only.

Normalization MUST preserve:
- immutable source-to-output span alignment;
- semantic/lexical identity separate from surface form;
- reversible provenance from output token to selected Claim and Evidence;
- explicit ambiguity and unresolved alternatives;
- deterministic locale/style policy;
- bounded output expansion and memory;
- idempotent repeated normalization under the same input, pack digest and policy.

A normalization result is not considered committed until the SURGE Core journal records the selected decision and output revision. Streaming provisional output may be revised; committed spans are immutable unless a new explicit revision event is recorded.

## 14. Normalization conformance additions

A conforming implementation additionally tests:
1. semantic candidate selection and orthographic realization are separate, traceable stages;
2. competing kanji/kana/katakana/Latin/digit/punctuation hypotheses are represented without premature collapse;
3. context can change the selected surface form without corrupting semantic identity;
4. renderer conflicts return to Core rather than being silently resolved inside the adapter;
5. source/output span alignment survives streaming revisions and journal replay;
6. locale/style changes are explicit policy changes and produce auditable revisions;
7. identical input + Language Set digest + policy produces deterministic output;
8. no application-side normalization path is invoked in production.

## 15. Implementation status update (2026-11-19)

The Core now has an initial UnifiedDecision C ABI in `include/surge/decision_c_api.h`. It accepts flat caller-owned candidate, evidence, surface-set and hypothesis arrays with checked offset/count ranges, returns the selected candidate and exact surface-hypothesis indices, and leaves buffer ownership with the caller. The C ABI version is explicitly queryable.

The first streaming resource reservation primitive is implemented in `include/surge/resource_budget.hpp`. It accounts resident bytes, transient bytes, retained events/clusters, decision candidates and evidence through move-only RAII leases. This is logical accounting, not a process RSS limiter, and it is not yet wired into every allocator or graph/journal path.

UnifiedDecision now defers when all valid evidence has zero effective contribution, rejects non-finite/invalid thresholds, and bounds aggregate surface sets, hypotheses, traces and surface string sizes. Cluster membership IDs use length-prefixed hash preimages; membership validation checks parent/member node existence and relation identity before mutating the caller's delta.

Not yet implemented: the full Language Runtime ABI and package loader, data-only package parser/signature/digest validation, session-pinned activation, generic morphology/lexicon/n-gram runtime, Japanese resource migration, second-language pack, end-to-end Python adapter replacement, and complete resident/transient budget enforcement across EventJournal/ContextGraph/recognition backends. These remain explicit L1-L7 work; the new C ABI is a bridge, not proof of end-to-end integration.

### 15.1 Bounded Language Set registry foundation

`include/surge/language_runtime.hpp` / `src/language_runtime.cpp` now provide a bounded manifest registry with per-set package/unpacked/resident/scratch declarations, aggregate installed package/unpacked/resident accounting, capability-name validation, session count limits, explicit session-boundary activation, session pinning, active-manifest lookup, and protection against uninstalling a pinned set. Switching a session pin is transactional at the registry level: the previous pin remains until the requested installed set and boundary have been validated.

This is a metadata registry and session-pinning foundation only. It does not yet read package files, verify cryptographic digests, load morphology/lexicon/n-gram resources, execute language modules, or guarantee that declared memory equals actual RSS. Those are still loader/runtime implementation tasks.
