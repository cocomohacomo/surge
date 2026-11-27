# SURGE Model Matrix Reproducibility Lock v1.3

The model matrix MUST NOT resolve mutable `main` or `master` refs at execution time.

## Immutable sources

| Component | Repository | Revision |
|---|---|---|
| llama.cpp CPU runtime | https://github.com/ggml-org/llama.cpp | `fd4b137e38c9fdb24b3f4fe3bc9e5fcc3c8f0002` |
| Qwen3 0.6B Q4_K_M GGUF | https://huggingface.co/gvij/qwen3-0.6b-gguf | `d86af45` |
| SmolLM2 135M Instruct Q4_K_M GGUF | https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF | `09816ac` |

Qwen3 0.6B is the only enabled candidate in the current matrix. SmolLM2 135M is disabled as an active candidate because the preceding unconstrained run produced invalid JSON in all 8/8 cases; its pinned entry remains as historical negative-control provenance.

The two Hugging Face revisions are commit identifiers displayed for the corresponding repository heads at lock creation time. They are supplied as explicit `revision` values to `hf_hub_download`; model and embedded GGUF tokenizer metadata resolve from the same repository revision. The downloaded model file SHA-256 is recorded per model in the experiment report.

The runtime commit is a full 40-character Git SHA and CI verifies the checked-out HEAD equals it before compilation.

## Lock file

Canonical machine-readable source: `config/model-matrix-lock-v1.0.json`.

Changing any repository, revision, file, quantization, tokenizer source, or runtime commit requires an explicit lock-file change and review.

## Experiment manifest

Each model matrix report includes `surge-experiment-manifest-v1.3` with:
- source commit, llama.cpp repository and immutable commit; execution backend, prompt mode and prompt transport;
- model lock SHA-256;
- model and tokenizer repositories/revisions;
- downloaded model file SHA-256 and byte count;
- Python, platform, machine, processor, Hugging Face Hub client and llama-completion version;
- thread count, timeout and generation parameters;
- system prompt SHA-256, prompt-template SHA-256, Canonical Projector Schema SHA-256, corpus SHA-256 and profile SHA-256.
- constrained generation via llama.cpp `--json-schema-file` using the exact Canonical Projector Schema v1.0; independent post-generation JSON Schema validation remains mandatory.
- exact-stdout JSON parsing: banners, echoed prompts, Markdown fences and multiple objects are rejected rather than scanned for a brace-delimited fragment.
- the exact terminal footer ' [end of text]' emitted by the pinned completion executable is stripped; other non-JSON output remains rejected.
- ChatML is supplied through a temporary UTF-8 prompt file (-f), not a process-list-visible (-p) argument.

This manifest improves reproducibility but does not by itself qualify Recognition Quality. The checked-in seed corpus remains synthetic and non-independent.

## Generation contract

The runner passes `schemas/surge-projector-output-v1.0.schema.json` directly to the pinned non-interactive llama-completion executable using `--json-schema-file`. It supplies preformatted ChatML through a temporary UTF-8 file (-f), and never places the prompt body in the child process arguments. It also runs the independent Draft 2020-12 validator on every parsed response. Constrained decoding only guarantees structural constraints supported by the runtime; Evidence Span offsets/quotes and semantic correctness remain separately scored by the evaluator.

## Artifact handling

The checked-in GitHub Actions workflow executes only the public synthetic seed corpus. The generated matrix report contains model outputs and may contain source-derived values and Evidence quotes. Do not upload reports produced from private or personal corpora to a public artifact store. A temporary prompt file prevents exposure through process arguments and GNU time command-line diagnostics, but it does not sanitize model output or downstream artifacts.
