# Projector Semantic Precision/Recall Evaluation v0.2

Status: evaluation contract for external Projector quality measurement.

## 1. Why v0.1 is insufficient

Marker recall alone does not penalize unrelated or duplicate candidates. A model can emit every expected marker plus arbitrarily many spurious candidates and still score perfectly. v0.2 therefore counts true positives, false positives, and false negatives separately.

## 2. Matching contract

- Candidate predictions are the text fields in `parsed_output.candidates`.
- Gold candidate markers are `expected_candidate_markers`.
- Candidate matching uses case-folded, whitespace-normalized substring matching in either direction. Each gold marker can match only one predicted candidate; duplicate predictions count as false positives.
- Relation predictions are `parsed_output.relations[*].type`.
- Relation labels use exact normalized matching. Unexpected or duplicate labels count as false positives.
- Negative cases have empty gold arrays. Any emitted candidate or relation is therefore counted as a false positive.

This is a transparent lexical proxy, not semantic entailment or paraphrase understanding. Evidence-span grounding and adjudicated candidate IDs are future extensions.

## 3. Metrics

For each case and model, aggregate:
- Precision = TP / (TP + FP)
- Recall = TP / (TP + FN)
- F1 = harmonic mean of precision and recall

The current smoke quality gate requires precision >= 0.50 and recall >= 0.50. It is reported independently from JSON and structural validity. Quality-gate failure does not fail the model-runtime smoke job; it records that the model did not meet the semantic threshold.

## 4. Input validation

The evaluator rejects missing case IDs, non-array gold fields, empty gold labels, malformed candidate/relation objects, and empty model result sets. Duplicate predictions are not silently deduplicated.

## 5. Limits

The five synthetic Japanese cases are a regression challenge set, not a statistically valid benchmark. Lexical matching misses paraphrases and may reward coincidental string overlap. A passing result does not establish truth, relevance, or general Japanese understanding. A larger adjudicated corpus with evidence spans, negative examples, and inter-annotator agreement is required before model selection based on quality.
