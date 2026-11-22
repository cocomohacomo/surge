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


if __name__ == "__main__":
    unittest.main()
