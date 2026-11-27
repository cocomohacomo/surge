# SURGE Seed Corpus / Evaluator Detailed Code Review
Date: 2026-11-27
Branch: `feat/semantic-gold-corpus-model-gates`
Scope: Canonical Gold seed migration, evidence spans, phenomenon metrics, category gates, model lock, experiment manifest and CI workflow.

## Review disposition

Static detailed review completed. Earlier findings were corrected and verified by the d91ca31 CI run: the pinned completion binary built, 8/8 synthetic model cases produced valid schema-conforming JSON, and the Core Integrity C++/CTest/Python workflow succeeded. Recognition Quality and full Core Integrity qualification remain NOT_RUN by design.

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
- Missing or malformed metric dimensions, Boolean-as-integer values, negative counters and incomplete expected model counts fail execution.
- Category and phenomenon metrics are aggregated per model.

### High: Model/runtime provenance was mutable or incomplete
- Added `config/model-matrix-lock-v1.0.json`.
- llama.cpp is fetched and checked out at full commit `fd4b137e38c9fdb24b3f4fe3bc9e5fcc3c8f0002`; CI verifies HEAD equality.
- Hugging Face model and embedded-tokenizer sources use explicit commit revisions `d86af45` and `09816ac`, not mutable branch names. These are short commit identifiers surfaced by the Hub and are pinned as explicit `revision` values. Full 40-character Hub SHAs remain preferable if available.
- Hugging Face Hub client is pinned in `requirements-model-matrix.txt`.
- The experiment manifest records model file hashes, model/tokenizer revisions, llama.cpp commit, runtime/platform, CLI version, threads, generation settings, system prompt hash, full prompt-template hash, lock hash and corpus/profile hashes.
- The runner now executes the same prompt template whose digest is recorded.
- Lock and corpus paths resolve relative to the repository root.

### Critical: Evidence spans must be bound to the exact attribute/relation
- The earlier evaluator pooled all raw spans in each dimension. A prediction could attach a valid span from a different attribute or relation and still receive Evidence Span F1 credit.
- Attribute span scoring now uses (key, value, polarity, category, source, start, end, quote) identity. Relation span scoring uses (type, source, target, source, start, end, quote).
- Reusing one source span for distinct attributes is valid and is no longer counted as a duplicate. An evidence span attached to the wrong semantic object becomes an FP, while the missing correct link becomes an FN.
- Regressions cover cross-attribute and cross-relation attachment, category mismatch, and valid reuse of a single span by distinct attributes.

### High: Prompt contents were visible in process arguments and timing diagnostics
- Passing ChatML through llama-completion -p placed the complete prompt in the operating-system process argument list. GNU time -v includes the command line in stderr, which the report persisted.
- Runner now writes the prompt to a temporary UTF-8 file and passes -f <path>. The process arguments contain only the temporary file path, not the prompt text. The temporary directory is removed after generation and diagnostic collection.
- The matrix report still contains model output and extracted content. CI currently uses a synthetic public seed corpus only; reports generated from private corpora must be handled as sensitive artifacts and must not be uploaded to public CI artifacts.

### Remaining evaluator boundary
- Phenomenon predictions are currently checked for an allowed label and valid source offsets, but their evidence is not yet linked to label-specific adjudicated Gold phenomenon spans.
- Abstention outputs are schema-validated but do not yet have their own precision/coverage/selective-risk metrics. These require an explicit adjudication contract and must be completed before a held-out benchmark claims to measure abstention quality.

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


## Evidence span generation hardening (2026-11-28)

- Removed fixed prompt examples that contained reusable offsets and quotes; the compact rule now requires offsets to be calculated from the current `text` or `context` and the quote to exactly equal that source slice.
- Lowered generation temperature from 0.8 to 0.2. This improves repeatability but is not a semantic-quality claim.
- Added strict Evidence Span validation for attributes, relations, phenomena, and abstentions. Emitted objects must contain evidence spans with a valid source, non-boolean integer offsets, nonempty quote, and exact source-slice equality.
- Evidence validity is now a separate gate and participates in the overall model-matrix gate. The seed corpus remains non-independent; this change does not qualify Recognition Quality.
