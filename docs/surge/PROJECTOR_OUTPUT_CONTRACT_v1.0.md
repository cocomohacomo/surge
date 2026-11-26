# SURGE Projector Output Contract v1.0

Status: normative wire-format contract for external recognition Projectors.
Schema: `schemas/surge-projector-output-v1.0.schema.json` (JSON Schema Draft 2020-12).

## 1. Authority boundary

The Projector proposes semantic attributes, relations, categories, polarity and evidence. The SURGE Core validates structure and transaction integrity; it does not infer missing semantic truth, rewrite producer labels or normalize producer weights. Candidate output is not a committed graph membership.

## 2. Canonical envelope

Every canonical output MUST contain `schema_version`, `attributes`, and `relations`. `schema_version` MUST equal `surge-projector-output-v1.0`. Unknown properties are rejected. Abstentions may be emitted separately and MUST include a non-empty reason.

## 3. Attribute

Each attribute MUST contain `key`, `value`, `polarity`, `category`, and `evidence_spans`. Polarity is one of `positive`, `negative`, `uncertain`, or `unknown`. Category is an explicit non-empty label. An empty evidence span list is allowed only when the Projector has no defensible source span; this is not evidence support and is scored accordingly.

## 4. Relation

Each relation MUST contain `type`, `source`, `target`, and `evidence_spans`. Endpoints are stable producer-side identifiers or labels within the current recognition envelope. The schema does not assert that the relation is true or that its endpoints are committed Core nodes.

## 5. Evidence span coordinate system

An Evidence Span contains `source`, `start`, `end`, and `quote`. Source is `context` or `text`. Offsets count Unicode code points in the exact corresponding input string, with zero-based start and exclusive end. `quote` MUST equal the exact substring `source_text[start:end]`. Byte offsets, UTF-16 code units, normalized-text offsets and offsets into concatenated context are prohibited.

Gold annotations use the same coordinate system. Evaluation performs exact span identity matching over source, start, end and quote. Attribute and relation evidence spans are scored independently and together. Invalid spans count as malformed and do not support an attribute or relation.

## 6. Separate validation layers

1. JSON decoding: output is syntactically valid JSON.
2. Schema validation: output conforms to the canonical JSON Schema.
3. Evidence integrity: every span resolves to the exact original input substring.
4. Semantic evaluation: attributes, categories, relations, duplicates, evidence support and span precision/recall are compared with independently adjudicated Gold.

A pass at one layer MUST NOT imply a pass at a later layer. Seed examples are engineering fixtures only and MUST NOT qualify Recognition Quality.

## 7. Compatibility and migration

Legacy `evidence_quote`, `evidence`, `candidates`, implicit category and missing schema-version forms are non-canonical. They may be retained in diagnostic adapters during migration but MUST NOT pass the canonical schema gate. New model-matrix runs use the v1.0 schema prompt and validator.

## 8. Qualification

Recognition Quality requires an independently adjudicated held-out corpus, a pre-registered benchmark profile, complete case coverage and passing schema, semantic and evidence-span thresholds. Missing Gold evidence spans or missing thresholds fail closed when evidence-span evaluation is enabled.

## 9. Seed corpus migration and phenomenon annotations

The engineering seed corpus uses corpus schema version `surge-gold-corpus-v1.0`. Every Gold attribute and relation carries one or more exact Evidence Spans. These examples are synthetic engineering fixtures only; their spans are not independently adjudicated and do not qualify Recognition Quality.

Held-out test cases MUST carry an explicit `phenomena` array drawn from:
- `negation`
- `contradiction`
- `temporal_supersession`
- `ambiguity`

The evaluator reports case-level and aggregate Precision, Recall and F1 independently for each phenomenon. The operational detector mapping is fixed: negative-polarity attributes indicate negation; contradiction-category attributes or `contradicts` relations indicate contradiction; temporal-update attributes or `supersedes` relations indicate temporal supersession; ambiguity-category attributes or explicit abstentions indicate ambiguity. This is a benchmark annotation contract, not a Core-side semantic inference rule.

## 10. Category thresholds

The benchmark profile MUST declare both `category_precision_min` and `category_recall_min`. Their key sets MUST exactly equal the Gold-supported attribute categories in the held-out test split. Every threshold is a finite number in [0,1]. Missing, extra, malformed or unsupported category floors fail closed.
