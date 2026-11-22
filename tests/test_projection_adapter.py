import copy
import json
import pathlib
import unittest

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

    def test_evidence_must_retain_source_packet_lineage(self):
        batch = self.fixture("document")
        evidence = next(node for node in batch["nodes"] if node["kind"] == "EVIDENCE")
        evidence["references"] = []
        with self.assertRaisesRegex(ProjectionRejected, "EVIDENCE_SOURCE_LINEAGE_MISSING"):
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


if __name__ == "__main__":
    unittest.main()
