# SURGE Model Matrix Reproducibility Lock v1.0

The model matrix MUST NOT resolve mutable `main` or `master` refs at execution time.

## Immutable sources

| Component | Repository | Revision |
|---|---|---|
| llama.cpp CPU runtime | https://github.com/ggml-org/llama.cpp | `fd4b137e38c9fdb24b3f4fe3bc9e5fcc3c8f0002` |
| Qwen3 0.6B Q4_K_M GGUF | https://huggingface.co/gvij/qwen3-0.6b-gguf | `d86af45` |
| SmolLM2 135M Instruct Q4_K_M GGUF | https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF | `09816ac` |

The two Hugging Face revisions are commit identifiers displayed for the corresponding repository heads at lock creation time. They are supplied as explicit `revision` values to `hf_hub_download`; model and embedded GGUF tokenizer metadata resolve from the same repository revision. The downloaded model file SHA-256 is recorded per model in the experiment report.

The runtime commit is a full 40-character Git SHA and CI verifies the checked-out HEAD equals it before compilation.

## Lock file

Canonical machine-readable source: `config/model-matrix-lock-v1.0.json`.

Changing any repository, revision, file, quantization, tokenizer source, or runtime commit requires an explicit lock-file change and review.

## Experiment manifest

Each model matrix report includes `surge-experiment-manifest-v1.0` with:
- source commit, llama.cpp repository and immutable commit;
- model lock SHA-256;
- model and tokenizer repositories/revisions;
- downloaded model file SHA-256 and byte count;
- Python, platform, machine, processor, Hugging Face Hub client and llama-cli version;
- thread count, timeout and generation parameters;
- system prompt SHA-256, corpus SHA-256 and profile SHA-256.

This manifest improves reproducibility but does not by itself qualify Recognition Quality. The checked-in seed corpus remains synthetic and non-independent.
