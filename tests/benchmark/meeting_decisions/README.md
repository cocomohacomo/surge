# Independent gold corpus staging area

No gold records are committed here yet. This is intentional: developer-authored
or model-generated labels must not be presented as independent ground truth.

Create versioned `cases.jsonl`, `gold.jsonl`, and a manifest only after
permission review, double annotation and adjudication. Split by complete
meeting. The manifest must include source/gold/split digests, consent and
license basis, redaction policy, annotation-guide version, pseudonymous
annotator IDs, adjudicator record and taxonomy version.

Use the evaluation protocol in `docs/evaluation/MEETING_DECISIONS_EVALUATION_v0.1.md`.
