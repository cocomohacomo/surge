# SURGE Seed Corpus / Evaluator Detailed Code Review
Date: 2026-11-27
Branch: `feat/semantic-gold-corpus-model-gates`
Scope: Canonical Gold seed migration, evidence spans, phenomenon metrics, category gates, model lock, experiment manifest and CI workflow.

## Review disposition

Static detailed review completed. Findings in this review were corrected before the dedicated CI verification pass. No runtime or CI success is claimed by this document.

## Findings and corrections

### Critical: Gold seed objects were not canonical envelopes
The earlier Gold objects omitted `schema_version`, so the seed could not be validated using the same canonical contract as Projector output.
- Added `schema_version=surge-projector-output-v1.0` to every Gold envelope.
- Held-out evaluation now rejects unsupported Gold Corpus versions, missing Gold Evidence Spans and Gold envelopes that fail the canonical Projector Schema.
- Added a fixture-wide test that validates every Gold envelope and verifies every span resolves to the exact source substring.

### Critical: Evidence offsets and evidence coverage needed stronger guarantees
- Added nonempty exact Evidence Spans to every seed Gold attribute and relation.
- Cross-context relations now cite source spans from both the earlier context and the new text where relevant.
- Corrected a test's off-by-one predicted span.
- Held-out evaluation rejects any Gold attribute or relation with no span.
- Invalid Gold offsets/quotes now return a structured FAIL report instead of an uncaught evaluator exception.
- Attribute and relation span metrics remain separately reported and are combined only for the overall span score.

### Critical: Category gate was recall-only
- Both `category_precision_min` and `category_recall_min` are mandatory.
- Each map must exactly cover the Gold-supported categories in the held-out split; missing and unexpected categories fail.
- Every threshold must be a finite number in [0,1].

### Critical: Phenomena were not independently scored
Added separate binary Precision / Recall / F1 for:
- Negation
- Contradiction
- Temporal Supersession
- Ambiguity

Held-out cases must explicitly annotate the four-label vocabulary. Profiles must provide exact precision and recall threshold maps for all four. Each phenomenon must have both positive and negative Gold support, avoiding unmeasurable one-class metrics. The operational mapping is documented and does not move semantic inference into Core.

### High: Model Matrix did not enforce the new dimensions
- Summary validation now requires category, all four phenomenon, attribute span and relation span metrics per case.
- Missing or malformed metric dimensions fail execution.
- Category and phenomenon metrics are aggregated per model.

### High: Model/runtime provenance was mutable or incomplete
- Added `config/model-matrix-lock-v1.0.json`.
- llama.cpp is fetched and checked out at full commit `fd4b137e38c9fdb24b3f4fe3bc9e5fcc3c8f0002`; CI verifies HEAD equality.
- Hugging Face model and embedded-tokenizer sources use explicit commit revisions `d86af45` and `09816ac`, not mutable branch names. These are short commit identifiers surfaced by the Hub and are pinned as explicit `revision` values. Full 40-character Hub SHAs remain preferable if available.
- Hugging Face Hub client is pinned in `requirements-model-matrix.txt`.
- The experiment manifest records model file hashes, model/tokenizer revisions, llama.cpp commit, runtime/platform, CLI version, threads, generation settings, system prompt hash, full prompt-template hash, lock hash and corpus/profile hashes.
- The runner now executes the same prompt template whose digest is recorded.
- Lock and corpus paths resolve relative to the repository root.

## Residual limitations

1. The eight-case corpus is synthetic and explicitly marked `independent_adjudication=false`. It is an engineering fixture, not a benchmark and cannot qualify Recognition Quality.
2. Model revision identifiers are short commit prefixes rather than full 40-character SHAs. They are explicit immutable revision selectors, but should be expanded to full SHAs when Hub metadata can be retrieved reliably.
3. The experiment manifest records the downloaded model-file SHA after retrieval; this run still needs CI evidence to confirm the locked revisions resolve and the files download successfully.
4. Core Integrity remains NOT_RUN. This work does not certify Snapshot baseline trust, complete Journal replay, full Graph reconciliation or crash recovery.

## Required CI interpretation

- Unit and workflow success validates code execution and test coverage only.
- Model Matrix execution PASS does not imply Recognition Quality PASS.
- Seed Corpus evaluation must remain NOT_RUN for qualification because it is not independently adjudicated.
- Aggregate Gate must remain FAIL until both independent Core Integrity and Recognition Quality evidence tracks pass with identical commit/profile/corpus identity.
- PR remains unmerged unless separately authorized.
