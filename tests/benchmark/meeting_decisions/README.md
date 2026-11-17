# Independent gold corpus staging area

No real gold records are committed here. This is intentional: developer-authored
or model-generated labels must not be presented as independent ground truth.

## Required frozen bundle

Prepare these four files outside the repository until privacy, licensing and
consent review is complete:

- `cases.jsonl`: immutable meeting records with `case_id`, `meeting_id`,
  `source_id`, `split` (train/dev/test), NFC `source_text` and SHA-256.
- `gold.jsonl`: adjudicated decision/action items, owner, deadline, criticality
  and exact half-open UTF-8 byte evidence spans.
- `annotations.jsonl`: two independent annotator records and a separately
  identified adjudication record for every case.
- `manifest.json`: `status: FROZEN`, exact file digests, source rights and
  redaction register, pseudonymous annotator/adjudicator rosters, guide/taxonomy
  versions, split counts and pilot sample minimums.

The validator rejects source-digest drift, missing or duplicated cases,
meeting-level split leakage, missing rights records, fewer than two independent
annotations, unresolved adjudication, gold/adjudication divergence and evidence
spans that do not match the normalized source bytes.

## Freeze and validate

```sh
python tools/validate_meeting_decision_corpus.py \
  --cases cases.jsonl --gold gold.jsonl \
  --annotations annotations.jsonl --manifest manifest.json
```

The default pilot minimum is 100 distinct meetings, 500 adjudicated items and
50 critical items. Lower thresholds may be used in unit tests only. A valid
frozen corpus below the minimum is reported as `INCOMPLETE_PILOT_SAMPLE`, not
as a passing evaluation. The held-out test split must remain sealed during
prompt/model development.

Synthetic fixtures in the unit tests exercise the validator only. They are
not a corpus, gold data or evidence of SURGE effectiveness.
