import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from surge_adapter.compact_recognition import (
    CompactModelSpec, CompactRecognitionRuntime, RecognitionCandidate,
    summarize_rtf, DEFAULT_MODEL_LIMIT_BYTES, RecognitionPacketMetadata,
    build_recognition_packet,
)


class FakeBackend:
    def recognize(self, text, context=None):
        return [RecognitionCandidate(
            "CLAIM", "予算", "予算を確保する", "POSITIVE", False,
            .95, .08, .9, 0, len(text.encode("utf-8")),
        )]


class CompactRecognitionTests(unittest.TestCase):
    def make_spec(self, path):
        return CompactModelSpec.inspect("test-ja-small", "1.0", str(path), runtime_id="fake-cpu")

    def test_artifact_inspection_and_recognition_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"tiny-model")
            spec = self.make_spec(model)
            runtime = CompactRecognitionRuntime(spec, FakeBackend())
            result = runtime.recognize("予算を確保する", {"topic": "予算"},
                                       audio_duration_seconds=10.0)
            self.assertEqual(result.model_bytes, len(b"tiny-model"))
            self.assertEqual(result.candidates[0].kind, "CLAIM")
            self.assertIsNotNone(result.rtf)
            self.assertEqual(result.performance_target_met, result.rtf < .01)
            self.assertEqual(result.as_dict()["model_id"], "test-ja-small")

    def test_packet_builder_creates_schema_valid_source_packet_and_candidate_sidecar(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            runtime = CompactRecognitionRuntime(self.make_spec(model), FakeBackend())
            execution = runtime.recognize("予算を確保する")
            metadata = RecognitionPacketMetadata(
                packet_id="p-1", idempotency_key="i-1", context_id="ctx",
                source_id="stream", block_ref="turn-1", source_order=1,
                source_generation="g1", source_sequence=1, state_version=0,
                adapter_id="compact-local", adapter_version="0.1",
                capability_descriptor_ref="cap-compact-1",
            )
            built = build_recognition_packet("予算を確保する", execution, metadata)
            self.assertEqual(built.packet["content"]["normalized_digest"], built.proposal_batch["source_digest"])
            self.assertEqual(built.proposal_batch["packet_id"], "p-1")
            self.assertEqual(built.proposal_batch["candidates"][0]["kind"], "CLAIM")

    def test_packet_builder_rejects_mismatched_source_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            runtime = CompactRecognitionRuntime(self.make_spec(model), FakeBackend())
            execution = runtime.recognize("source A")
            metadata = RecognitionPacketMetadata("p", "i", "ctx", "src", "b", 1, "g1", 1, 0, "adapter", "1", "cap")
            with self.assertRaisesRegex(ValueError, "PACKET_EXECUTION_SOURCE_MISMATCH"):
                build_recognition_packet("source B", execution, metadata)

    def test_runtime_normalizes_nfc_before_backend_offsets(self):
        class EchoBackend:
            def recognize(self, text, context=None):
                return [RecognitionCandidate("CLAIM", None, text, "POSITIVE", False, .9, .1, .8, 0, len(text.encode("utf-8")))]
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            runtime = CompactRecognitionRuntime(self.make_spec(model), EchoBackend())
            result = runtime.recognize("か\\u3099")
            self.assertEqual(result.candidates[0].end_offset, len("が".encode("utf-8")))

    def test_revision_packet_requires_explicit_lineage(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            runtime = CompactRecognitionRuntime(self.make_spec(model), FakeBackend())
            execution = runtime.recognize("text")
            metadata = RecognitionPacketMetadata("p", "i", "ctx", "src", "b", 1, "g1", 1, 0, "adapter", "1", "cap", finalization="REVISION")
            with self.assertRaisesRegex(ValueError, "PACKET_REVISION_LINEAGE_REQUIRED"):
                build_recognition_packet("text", execution, metadata)

    def test_rtf_is_not_claimed_without_media_duration(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            result = CompactRecognitionRuntime(self.make_spec(model), FakeBackend()).recognize("はい")
            self.assertIsNone(result.rtf)
            self.assertIsNone(result.performance_target_met)

    def test_artifact_mutation_after_inspection_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"original")
            spec = self.make_spec(model)
            model.write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "MODEL_ARTIFACT_DIGEST_CHANGED"):
                CompactRecognitionRuntime(spec, FakeBackend())

    def test_model_size_limit_is_enforced(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "oversized.bin"
            with model.open("wb") as stream:
                stream.truncate(DEFAULT_MODEL_LIMIT_BYTES + 1)
            with self.assertRaisesRegex(ValueError, "MODEL_ARTIFACT_SIZE_LIMIT"):
                self.make_spec(model)

    def test_empty_model_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "empty.bin"
            model.touch()
            with self.assertRaisesRegex(ValueError, "MODEL_ARTIFACT_EMPTY"):
                self.make_spec(model)

    def test_invalid_duration_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            runtime = CompactRecognitionRuntime(self.make_spec(model), FakeBackend())
            with self.assertRaisesRegex(ValueError, "AUDIO_DURATION_INVALID"):
                runtime.recognize("text", audio_duration_seconds=0)

    def test_benchmark_summary_requires_real_measurements(self):
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            runtime = CompactRecognitionRuntime(self.make_spec(model), FakeBackend())
            unmeasured = runtime.recognize("text")
            self.assertIsNone(summarize_rtf([unmeasured])["target_pass"])
            measured = runtime.recognize("text", audio_duration_seconds=1000)
            self.assertIsNone(summarize_rtf([unmeasured, measured])["target_pass"])
            measured = runtime.recognize("text", audio_duration_seconds=1000)
            report = summarize_rtf([measured])
            self.assertEqual(report["measured_count"], 1)
            self.assertEqual(report["target_pass"], measured.rtf < .01)

    def test_backend_offsets_must_fit_utf8_source(self):
        class BadOffsetBackend:
            def recognize(self, text, context=None):
                return [RecognitionCandidate("CLAIM", None, "x", "POSITIVE", False, .9, .1, .9, 0, 999)]
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            runtime = CompactRecognitionRuntime(self.make_spec(model), BadOffsetBackend())
            with self.assertRaisesRegex(ValueError, "BACKEND_CANDIDATE_OFFSET_OUT_OF_RANGE"):
                runtime.recognize("日本語")

    def test_backend_cannot_return_untyped_candidates(self):
        class BadBackend:
            def recognize(self, text, context=None):
                return [{"kind": "CLAIM"}]
        with tempfile.TemporaryDirectory() as tmp:
            model = pathlib.Path(tmp) / "model.bin"
            model.write_bytes(b"model")
            runtime = CompactRecognitionRuntime(self.make_spec(model), BadBackend())
            with self.assertRaisesRegex(ValueError, "BACKEND_CANDIDATE_TYPE_INVALID"):
                runtime.recognize("text")


if __name__ == "__main__":
    unittest.main()
