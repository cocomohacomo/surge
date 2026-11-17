# Meeting decision extraction: minimal adapter example

This directory demonstrates the common benchmark adapter contract for one
narrow use case: extracting explicit meeting decisions/actions with owner,
deadline and exact source evidence.

## Smoke test

From the repository root:

```sh
python tools/run_meeting_decision_adapter.py \
  --cases tests/benchmark/meeting_decisions/cases.jsonl \
  --adapter examples.meeting_decisions.demo_adapter:predict \
  --output build/demo-predictions.jsonl
```

The demo parser only recognizes explicit `決定:`, `担当:`, and `期限:`
lines. It is a plumbing test, not a semantic baseline or evidence of quality.
For production comparisons, implement the same `predict(case)` contract for
SURGE, simple RAG, structured DB, and the named incumbent.

Each input case must include `case_id`, `source_text`, and SHA-256 of NFC
UTF-8 source bytes. The adapter returns exactly `decision` and `items`.
See the evaluation protocol for the item shape, byte-offset convention and
equal-condition rules.

Do not put personal or confidential minutes in the public repository. Use a
permissioned, redacted, separately versioned corpus.
