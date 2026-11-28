# SURGE CPU Minimal Projector Benchmark v0.2

## Purpose

Evaluate small CPU-only instruction models for the new Minimal Projection contract.
The model receives one information item and a bounded list of existing cluster
candidates. It returns only:

- `information`: exact input echo
- `target_cluster`: one supplied cluster ID or `null`
- `distance`: numeric score

The benchmark deliberately excludes attribute extraction, relation generation,
phenomena, evidence spans, offsets, and explanatory prose. Those outputs are not
part of the minimal Core ingress contract.

## Candidate models

| Model | Quantization | Approximate file size | License | Role |
|---|---|---:|---|---|
| Qwen3-0.6B | Q4_K_M | 484 MB | Apache-2.0 | Japanese/multilingual primary candidate |
| SmolLM2-135M-Instruct | Q4_K_M | 105 MB | Apache-2.0 | Low-footprint baseline; primarily English |

Model weights are downloaded at benchmark time and are not committed to the repository.

## Inference budget

- CPU-only llama.cpp runtime
- 2 threads by default
- Maximum 64 generated tokens per case
- Temperature 0 and top-k 1
- Bounded candidate inventory supplied in the prompt
- No retry loop and no semantic repair

The benchmark measures elapsed time, tokens/second when reported by llama.cpp,
peak RSS, strict projection validity, and target-cluster accuracy.

## Distance semantics

For this benchmark only, `distance` is a similarity score from 0 to 100:
100 means the closest match; 0 is used for no candidate. This profile is named
`benchmark_similarity_0_100`.

This convention is not imposed on SURGE Core. Core stores the number as opaque
Projector output and does not compare, normalize, threshold, or interpret it.
A production Projector profile must declare its own stable score semantics.

## Cases

1. `柴犬` against dog and cat clusters.
2. `三毛猫` against dog and cat clusters.
3. Meeting deadline statement against meeting and deadline clusters.
4. Disaster application exception against application and weather clusters.
5. Unrelated astronomical information against dog and budget clusters, expecting no target.

## Quality gates and interpretation

A model output is valid only if it contains exactly the three contract keys,
echoes the input exactly, selects a supplied cluster ID or null, and returns an
integer score in the benchmark range. Target accuracy is reported separately
from structural validity.

These five synthetic cases are a smoke test, not a statistically valid Japanese
quality certification. SmolLM2 is primarily English-oriented and is a footprint
baseline, not a presumed production choice. GitHub runner latency and RSS are
comparative measurements, not measurements on the i5-10310 / 8 GB target.

No production model is selected by this benchmark alone.
