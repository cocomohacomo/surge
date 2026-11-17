# Compact ONNX CPU Backend v0.1

`CompactOnnxCpuBackend` implements the compact recognition backend contract
with ONNX Runtime and CPUExecutionProvider. It uses sequential execution,
full graph optimization, one inter-op thread and a configurable intra-op
thread count (default one). ONNX Runtime is imported lazily and is optional.

Tokenizer and CandidateDecoder are injected. The tokenizer supplies named
input tensors; the backend checks all graph inputs and ignores extra tokenizer
fields. The decoder maps named model outputs to typed RecognitionCandidate
records with UTF-8 byte offsets. This keeps vocabularies, label maps, tensor
layouts and output heads outside SURGE Core.

The model artifact is verified by CompactModelSpec, including the default
30 MiB ceiling and SHA-256. The backend is CPU-only. An explicit warmup method
is available and must be excluded from measured samples.

This is executable inference plumbing, not a pretrained Japanese model. Model
weights, tokenizer assets, label maps and decoder logic must be packaged and
license-checked together. The RTF < 0.01 target is not certified until a real
model is benchmarked on target hardware. Full-pipeline measurements must
include tokenization, inference, decoding, packet building, scoring and Core
admission.
