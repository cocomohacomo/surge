import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from surge_adapter.semantic_core_bridge import SemanticCoreBridge
from surge_adapter.packet_ingest import PacketRejected


class ClaimTemporalValidityBridgeTests(unittest.TestCase):
    def test_validity_revision_is_durable_and_idempotent(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "claim-validity.sgj"
            with SemanticCoreBridge(journal) as core:
                seeded = core.apply_semantic_delta({
                    "delta_id": "validity-seed",
                    "idempotency_key": "validity-seed",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": "evidence-1", "kind": "EVIDENCE",
                         "content": "契約書", "references": []},
                        {"id": "review-1", "kind": "EVIDENCE",
                         "content": "審査記録: 承認", "references": []},
                        {"id": "unrelated-evidence", "kind": "EVIDENCE",
                         "content": "無関係な資料", "references": []},
                        {"id": "claim-1", "kind": "CLAIM",
                         "content": "契約は2026年1月から有効",
                         "references": ["evidence-1"]},
                    ],
                    "relations": [],
                })
                self.assertEqual(seeded["state_version"], 1)
                result = core.apply_claim_validity(
                    delta_id="validity-revision-1",
                    idempotency_key="validity-revision-1",
                    expected_state_version=1,
                    claim_id="claim-1",
                    valid_from="2026-01-01T00:00:00+09:00",
                    valid_until="2026-04-01T00:00:00+09:00",
                    basis_evidence_id="evidence-1",
                    revision=1,
                )
                self.assertEqual(result["state_version"], 2)
                self.assertEqual(core.claim_validity_at(
                    "claim-1", "2025-12-31T15:00:00Z"), "VALID")
                self.assertEqual(core.claim_validity_at(
                    "claim-1", "2026-03-31T15:00:00Z"), "EXPIRED")
                duplicate = core.apply_claim_validity(
                    delta_id="validity-revision-1",
                    idempotency_key="validity-revision-1",
                    expected_state_version=1,
                    claim_id="claim-1",
                    valid_from="2026-01-01T00:00:00+09:00",
                    valid_until="2026-04-01T00:00:00+09:00",
                    basis_evidence_id="evidence-1",
                    revision=1,
                )
                self.assertTrue(duplicate["duplicate"])
                self.assertEqual(core.state_version, 2)
                with self.assertRaisesRegex(PacketRejected,
                                            "CLAIM_VALIDITY_INTERVAL_INVALID"):
                    core.apply_claim_validity(
                        delta_id="validity-invalid",
                        idempotency_key="validity-invalid",
                        expected_state_version=2,
                        claim_id="claim-1",
                        valid_from="2026-05-01T00:00:00Z",
                        valid_until="2026-04-01T00:00:00Z",
                        basis_evidence_id="evidence-1",
                        revision=2,
                    )
                self.assertEqual(core.state_version, 2)
                with self.assertRaisesRegex(PacketRejected, "CLAIM_VALIDITY_EVIDENCE_NOT_BOUND_TO_CLAIM"):
                    core.apply_claim_validity(
                        delta_id="validity-unrelated-evidence",
                        idempotency_key="validity-unrelated-evidence",
                        expected_state_version=2, claim_id="claim-1",
                        valid_from="2026-01-01T00:00:00Z", valid_until=None,
                        basis_evidence_id="unrelated-evidence", revision=2)
                self.assertEqual(core.state_version, 2)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version, 2)


    def test_approved_temporal_binding_commits_with_review_provenance_and_replays(self):
        from surge_adapter.temporal_recognition import recognize_temporal_spans
        from surge_adapter.temporal_claim_binding import bind_temporal_candidates
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "reviewed-validity.sgj"
            source = "契約Aは2026年4月1日から有効"
            spans = recognize_temporal_spans(source, timezone_offset="+09:00")
            binding = bind_temporal_candidates(source, spans, claim_id="claim-a",
                evidence_id="evidence-a", claim_accepted=True)[0]
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "review-seed", "idempotency_key": "review-seed",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": "evidence-a", "kind": "EVIDENCE", "content": source, "references": []},
                        {"id": "review-evidence", "kind": "EVIDENCE", "content": "審査記録: 承認", "references": []},
                        {"id": "claim-a", "kind": "CLAIM", "content": "契約Aは有効", "references": ["evidence-a"]},
                    ], "relations": []})
                with self.assertRaisesRegex(PacketRejected, "CLAIM_VALIDITY_REVIEW_NOT_APPROVED"):
                    core.apply_approved_temporal_binding(binding, source, approved=False,
                        reviewer_id="reviewer-1", review_evidence_id="review-evidence",
                        delta_id="reviewed-validity", idempotency_key="reviewed-validity",
                        expected_state_version=1, revision=1)
                self.assertEqual(core.state_version, 1)
                committed = core.apply_approved_temporal_binding(binding, source, approved=True,
                    reviewer_id="reviewer-1", review_evidence_id="review-evidence",
                    delta_id="reviewed-validity", idempotency_key="reviewed-validity",
                    expected_state_version=1, revision=1)
                self.assertEqual(committed["state_version"], 2)
                self.assertEqual(core.claim_validity_at("claim-a", "2026-04-01T00:00:00+09:00"), "VALID")
                with self.assertRaisesRegex(PacketRejected, "CLAIM_VALIDITY_SOURCE_SPAN_MISMATCH"):
                    bad = dict(binding)
                    bad["source_span"] = dict(binding["source_span"], exact_text="別の日付")
                    core.apply_approved_temporal_binding(bad, source, approved=True,
                        reviewer_id="reviewer-1", review_evidence_id="review-evidence",
                        delta_id="bad-span", idempotency_key="bad-span",
                        expected_state_version=2, revision=2)
                self.assertEqual(core.state_version, 2)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version, 2)
                self.assertEqual(recovered.claim_validity_at("claim-a",
                    "2026-04-01T00:00:00+09:00"), "VALID")

if __name__ == "__main__":
    unittest.main()
