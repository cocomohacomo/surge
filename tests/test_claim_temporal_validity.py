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
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version, 2)


if __name__ == "__main__":
    unittest.main()
