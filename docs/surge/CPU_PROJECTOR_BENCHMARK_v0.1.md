# SURGE CPU Projector Model Benchmark v0.1

## Purpose

Compare small, openly licensed GGUF instruction models as external Projector candidates for Japanese SURGE inputs. The benchmark does not alter SURGE Core's two primitives (cognition-information clusters and Focus). Model outputs remain untrusted proposals; Core validates structure and persists accepted projections without deciding truth or relevance.

## Initial candidates

| Model | Quantization | Approximate file size | License | Role |
|---|---|---:|---|---|
| Qwen3-0.6B | Q4_K_M | 484 MB | Apache-2.0 | Primary Japanese/multilingual candidate |
| SmolLM2-135M-Instruct | Q4_K_M | 105 MB | Apache-2.0 | Very-low-footprint baseline; primarily English |

Sources:
- Qwen3 model card: https://huggingface.co/Qwen/Qwen3-0.6B
- Qwen3 GGUF conversion: https://huggingface.co/gvij/qwen3-0.6b-gguf
- SmolLM2 model card: https://huggingface.co/HuggingFaceTB/SmolLM2-135M-Instruct
- SmolLM2 GGUF conversion: https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF

The GGUF files are downloaded individually by the benchmark. No model weights are committed to this repository.

## Execution

The `cpu-projector-model-benchmark` GitHub Actions job builds llama.cpp with CPU/OpenMP support, limits inference to two threads, downloads the GGUF files, and runs the same five Japanese prompts against both models.

Run locally after installing llama.cpp and `huggingface_hub`:

```sh
LLAMA_CLI=/path/to/llama-cli \
SURGE_MODEL_THREADS=2 \
python tools/benchmark_cpu_projectors.py
```

The JSON report records model file size, per-case wall time, llama.cpp token throughput when present in logs, peak RSS from `/usr/bin/time -v`, JSON parse success, candidate/relation counts, simple expected-marker coverage, and bounded output excerpts.

## Cases

1. Short contextual acknowledgement (「うん」).
2. Conflicting Tuesday/Wednesday deadline statements.
3. General rule and disaster exception.
4. Effective date across old and revised制度.
5. Cross-block reference resolution.

## Interpretation limits

- Five synthetic prompts are a smoke test, not a statistically valid Japanese benchmark.
- Marker coverage is only a weak lexical indicator. It is not semantic correctness.
- GitHub-hosted runner latency/RSS are comparative measurements, not measurements on a deployment target such as i5-10310 / 8 GB.
- Model quality must be evaluated separately for evidence-span fidelity, relation precision/recall, contradiction preservation, exception scope, date normalization, and hallucination rate.
- SmolLM2's official model card says it primarily understands and generates English. Its result is a size/latency baseline, not a likely Japanese production choice.
- No model is selected for production by this benchmark alone.
- No Focus geometry, Focus depth, or logical Focus region is changed by model inference.
