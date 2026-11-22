import hashlib
import json
import pathlib
import tempfile
import unittest

from surge_adapter.packet_ingest import CppCoreBridge
from surge_adapter.projection import ProjectionBatchAdapter, ProjectionRejected


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ProjectionBatchAdapterTests(unittest.TestCase):
    def setUp(self):
        self.adapter = ProjectionBatchAdapter()

    def fixture(self, name):
        path = ROOT / "tests" / "fixtures" / "projection" / f"{name}.json"
        return json.loads(path.read_text(encoding="utf-8"))

    def test_conversation_document_and_structured_share_one_contract(self):
        for name in ("conversation", "document", "structured"):
            with self.subTest(name=name):
                batch = self.fixture(name)
                source = self.adapter.validate(batch)
                self.assertEqual(source.decode("utf-8"), batch["normalized_source_snapshot"])
                self.assertEqual(batch["source_event_refs"], [batch["source_packet"]["packet_id"]])

    def test_source_digest_mismatch_is_rejected(self):
        batch = self.fixture("conversation")
        batch["source_packet"]["source_snapshot_digest"] = "f" * 64
        with self.assertRaisesRegex(ProjectionRejected, "SOURCE_SNAPSHOT_DIGEST_MISMATCH"):
            self.adapter.validate(batch)

    def test_projection_must_reference_its_source_packet(self):
        batch = self.fixture("document")
        batch["source_event_refs"] = ["different-packet"]
        with self.assertRaisesRegex(ProjectionRejected, "PROJECTION_SOURCE_PACKET_MISMATCH"):
            self.adapter.validate(batch)

    def test_source_packet_context_must_match_projection_context(self):
        batch = self.fixture("structured")
        batch["source_packet"]["context_id"] = "different-context"
        with self.assertRaisesRegex(ProjectionRejected, "PROJECTION_CONTEXT_PACKET_MISMATCH"):
            self.adapter.validate(batch)

    def test_document_global_range_length_mismatch_is_rejected(self):
        batch = self.fixture("document")
        batch["source_packet"]["global_end_offset"] += 1
        with self.assertRaisesRegex(ProjectionRejected, "DOCUMENT_GLOBAL_RANGE_LENGTH_MISMATCH"):
            self.adapter.validate(batch)

    def test_document_stream_envelope_must_be_complete(self):
        batch = self.fixture("document")
        del batch["source_packet"]["document_generation"]
        with self.assertRaisesRegex(ProjectionRejected, "DOCUMENT_STREAM_ENVELOPE_INCOMPLETE"):
            self.adapter.validate(batch)

    def test_non_finite_membership_weight_is_rejected(self):
        batch = self.fixture("conversation")
        batch["memberships"][0]["upstream_weight"] = float("nan")
        with self.assertRaisesRegex(ProjectionRejected, "NON_FINITE_MEMBERSHIP_WEIGHT"):
            self.adapter.validate(batch)

    def test_out_of_range_membership_weight_is_rejected(self):
        batch = self.fixture("conversation")
        batch["memberships"][0]["upstream_weight"] = 1.1
        with self.assertRaisesRegex(ProjectionRejected, "SCHEMA_INVALID"):
            self.adapter.validate(batch)

    def test_json_to_c_abi_to_core_commit_for_all_source_formats(self):
        for name in ("conversation", "document", "structured"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                batch = self.fixture(name)
                with CppCoreBridge(pathlib.Path(tmp) / "projection.sgj") as core:
                    committed = self.adapter.commit(batch, core)
                    self.assertFalse(committed.duplicate)
                    self.assertEqual(committed.state_version, 1)
                    self.assertEqual(committed.packet_status, "COMMITTED")
                    duplicate = self.adapter.commit(batch, core)
                    self.assertTrue(duplicate.duplicate)
                    self.assertEqual(duplicate.state_version, 1)
                    self.assertGreater(committed.input_bytes, 0)
                    self.assertEqual(committed.nodes_processed, 3)
                    self.assertGreater(committed.elapsed_microseconds, 0)
                    metrics = core.resource_snapshot
                    self.assertGreater(metrics["resident_bytes_used"], 0)
                    self.assertGreater(metrics["retained_events_used"], 0)
                    self.assertEqual(metrics["transient_bytes_used"], 0)
                with CppCoreBridge(pathlib.Path(tmp) / "projection.sgj") as recovered:
                    self.assertEqual(recovered.state_version, 1)
                    self.assertEqual(recovered.packet_count, 1)
                    self.assertGreater(
                        recovered.resource_snapshot["resident_bytes_used"], 0)

    def test_budget_rejection_happens_before_packet_or_graph_commit(self):
        batch = self.fixture("conversation")
        limits = {
            "resident_bytes": 1024 * 1024,
            "transient_bytes": 1,
            "retained_events": 1000,
            "retained_clusters": 100,
            "decision_candidates": 100,
            "decision_evidence": 100,
        }
        with tempfile.TemporaryDirectory() as tmp:
            with CppCoreBridge(pathlib.Path(tmp) / "budget.sgj", resource_limits=limits) as core:
                with self.assertRaisesRegex(ProjectionRejected, "RESOURCE_BUDGET_EXCEEDED"):
                    self.adapter.commit(batch, core)
                self.assertEqual(core.state_version, 0)
                self.assertEqual(core.packet_count, 0)
                self.assertEqual(core.resource_snapshot["transient_bytes_used"], 0)

    def test_multi_packet_stream_commits_in_order(self):
        first = self.fixture("conversation")
        second = self.fixture("document")
        def rewrite(value, mapping):
            if isinstance(value, dict):
                return {key: rewrite(item, mapping) for key, item in value.items()}
            if isinstance(value, list):
                return [rewrite(item, mapping) for item in value]
            if isinstance(value, str):
                for old, new in mapping.items():
                    value = value.replace(old, new)
            return value

        common = {
            "context-conversation": "context-stream",
            "source-conversation": "source-stream",
            "generation-1": "generation-stream",
            "packet-conversation": "packet-stream-1",
            "packet-document": "packet-stream-2",
            "packet-idem-conversation": "packet-idem-stream-1",
            "packet-idem-document": "packet-idem-stream-2",
        }
        first = rewrite(first, common)
        second = rewrite(second, {
            **common,
            "context-document": "context-stream",
            "source-document": "source-stream",
        })
        first["source_packet"]["block_ref"] = "block-stream-1"
        second["source_packet"]["block_ref"] = "block-stream-2"
        first["source_packet"]["source_sequence"] = 1
        second["source_packet"]["source_sequence"] = 2
        first["base_state_version"] = 0
        second["base_state_version"] = 1
        for batch in (first, second):
            snapshot = batch["normalized_source_snapshot"].encode("utf-8")
            digest = hashlib.sha256(snapshot).hexdigest()
            packet = batch["source_packet"]
            packet["source_snapshot_digest"] = digest
            packet["source_range_digest"] = hashlib.sha256(
                snapshot[packet["start_offset"]:packet["end_offset"]]).hexdigest()
            packet["canonical_request_digest"] = hashlib.sha256(
                packet["packet_id"].encode()).hexdigest()
            batch["source_event_refs"] = [packet["packet_id"]]

        with tempfile.TemporaryDirectory() as tmp:
            with CppCoreBridge(pathlib.Path(tmp) / "stream.sgj") as core:
                result = self.adapter.commit_stream([first, second], core)
                self.assertTrue(result.complete)
                self.assertEqual(result.committed_batches, 2)
                self.assertEqual(result.duplicate_batches, 0)
                self.assertEqual(result.next_index, 2)
                self.assertEqual(result.state_version, 2)
                self.assertEqual(core.packet_count, 2)
                self.assertGreater(result.input_bytes, 0)
                self.assertEqual(result.nodes_processed, 6)
                self.assertEqual(result.memberships_processed, 2)
                self.assertGreater(result.elapsed_microseconds, 0)
                replayed = self.adapter.commit_stream([first, second], core)
                self.assertTrue(replayed.complete)
                self.assertEqual(replayed.duplicate_batches, 2)
                self.assertEqual(replayed.state_version, 2)
                self.assertGreater(core.resource_snapshot["resident_bytes_used"], 0)
                self.assertEqual(core.resource_snapshot["transient_bytes_used"], 0)


if __name__ == "__main__":
    unittest.main()
