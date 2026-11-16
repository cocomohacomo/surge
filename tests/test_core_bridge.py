import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from surge_adapter.semantic_core_bridge import SemanticCoreBridge
from surge_adapter.packet_ingest import (
    CppCoreBridge, PacketRejected, RecognitionPacketAdapter, normalize_source,
)


def make_packet(source, packet_id="bridge-p1", sequence=1, idem="bridge-i1"):
    raw = normalize_source(source)
    digest = hashlib.sha256(raw).hexdigest()
    return {
        "schema_version": "0.9.0", "packet_id": packet_id,
        "idempotency_key": idem, "context_id": "bridge-ctx",
        "source_id": "bridge-source",
        "source_position": {
            "block_ref": "block-1", "start_offset": 0, "end_offset": len(raw),
            "offset_basis": "UTF8_BYTE_NFC", "source_order": sequence,
            "source_generation": "generation-1", "source_sequence": sequence,
            "source_range_digest": digest,
        },
        "structural": {"block_type": "PARAGRAPH", "semantic_role": "MAIN_CONTENT"},
        "content": {"text": source, "normalized_digest": digest, "language": "ja"},
        "context_hint": {"state_version": 0, "temporal_mode": "CURRENT",
                         "retrieval_reason": "integration-test"},
        "coverage": {"completeness": "COMPLETE"},
        "adapter": {"adapter_id": "integration-test", "adapter_version": "1",
                    "capability_descriptor_ref": "cap-bridge"},
        "finalization": "FINAL",
    }


class PythonCppRuntimeBridgeTests(unittest.TestCase):
    def test_nfc_admission_duplicate_and_restart_replay(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "runtime.sgj"
            adapter = RecognitionPacketAdapter()
            packet = make_packet("か\u3099")
            with CppCoreBridge(journal) as core:
                first = adapter.admit_to_core(packet, "か\u3099", core)
                self.assertEqual(first.status, "COMMITTED")
                self.assertEqual(core.state_version, 1)
                self.assertEqual(core.packet_count, 1)
                duplicate = adapter.admit_to_core(packet, "か\u3099", core)
                self.assertEqual(duplicate.status, "DUPLICATE")
                self.assertEqual(core.state_version, 1)
                self.assertEqual(core.packet_count, 1)
            with CppCoreBridge(journal) as recovered:
                duplicate = adapter.admit_to_core(packet, "か\u3099", recovered)
                self.assertEqual(duplicate.status, "DUPLICATE")
                self.assertEqual(recovered.state_version, 1)
                self.assertEqual(recovered.packet_count, 1)


    def test_temporal_boundary_c_api_bridge_persists_and_restores(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "temporal-ledger.sgj"
            with SemanticCoreBridge(journal) as core:
                first = core.add_temporal_boundary(
                    transaction_id="tb-tx-1", idempotency_key="tb-idem-1",
                    event_id="tb-event-1", expected_state_version=0,
                    claim_id="claim/bridge", role="VALID_FROM",
                    valid_from="2026-04-01T00:00:00+09:00",
                    evidence_id="evidence/1", packet_id="packet/1")
                self.assertEqual(first["status"], "NEEDS_OTHER_BOUNDARY")
                self.assertEqual(core.temporal_boundary_pending_count, 1)
                second = core.add_temporal_boundary(
                    transaction_id="tb-tx-2", idempotency_key="tb-idem-2",
                    event_id="tb-event-2", expected_state_version=1,
                    claim_id="claim/bridge", role="VALID_UNTIL",
                    valid_until="2026-05-01T00:00:00+09:00",
                    evidence_id="evidence/2", packet_id="packet/2")
                self.assertEqual(second["status"], "BOUNDARIES_COMPLETE_PENDING_REVIEW")
                self.assertEqual(len(second["pending"]["evidence_ids"]), 2)
                self.assertEqual(len(second["pending"]["candidates"]), 2)
                conflict = core.add_temporal_boundary(
                    transaction_id="tb-tx-3", idempotency_key="tb-idem-3",
                    event_id="tb-event-3", expected_state_version=2,
                    claim_id="claim/bridge", role="VALID_FROM",
                    valid_from="2026-03-01T00:00:00+09:00",
                    evidence_id="evidence/3", packet_id="packet/3")
                self.assertEqual(conflict["status"], "BOUNDARY_CONFLICT_REVIEW_REQUIRED")
                self.assertEqual(len(conflict["pending"]["candidates"]), 3)
                resolved = core.resolve_temporal_boundary_candidate(
                    transaction_id="tb-tx-4", idempotency_key="tb-idem-4",
                    event_id="tb-event-4", expected_state_version=3,
                    claim_id="claim/bridge", role="VALID_FROM", candidate_id="tb-event-1")
                self.assertEqual(resolved["status"], "CONFLICT_RESOLVED_PENDING_APPROVAL")
                self.assertEqual(resolved["pending"]["valid_from"], "2026-04-01T00:00:00+09:00")
            with SemanticCoreBridge(journal) as recovered:
                pending = recovered.get_temporal_boundary("claim/bridge")
                self.assertEqual(pending["valid_from"], "2026-04-01T00:00:00+09:00")
                self.assertEqual(pending["valid_until"], "2026-05-01T00:00:00+09:00")
                self.assertEqual(pending["packet_ids"], ["packet/1", "packet/2", "packet/3"])
                self.assertEqual(pending["status"], "CONFLICT_RESOLVED_PENDING_APPROVAL")
                self.assertEqual(len(pending["candidates"]), 3)
                self.assertEqual([c["candidate_id"] for c in pending["candidates"] if c["selected"]],
                                 ["tb-event-1"])
                cleared = recovered.clear_temporal_boundary(
                    transaction_id="tb-tx-5", idempotency_key="tb-idem-5",
                    event_id="tb-event-5", expected_state_version=4,
                    claim_id="claim/bridge")
                self.assertTrue(cleared["committed"])
                self.assertIsNone(recovered.get_temporal_boundary("claim/bridge"))

    def test_invalid_snapshot_rejected_before_core_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            adapter = RecognitionPacketAdapter()
            packet = make_packet("SURGE")
            with CppCoreBridge(pathlib.Path(tmp) / "runtime.sgj") as core:
                with self.assertRaisesRegex(PacketRejected, "SOURCE_SNAPSHOT_DIGEST_MISMATCH"):
                    adapter.admit_to_core(packet, "tampered", core)
                self.assertEqual(core.state_version, 0)
                self.assertEqual(core.packet_count, 0)

    def test_schema_rejection_does_not_touch_core(self):
        with tempfile.TemporaryDirectory() as tmp:
            adapter = RecognitionPacketAdapter()
            packet = make_packet("abc")
            packet["unexpected"] = True
            with CppCoreBridge(pathlib.Path(tmp) / "runtime.sgj") as core:
                with self.assertRaisesRegex(PacketRejected, "SCHEMA_INVALID"):
                    adapter.admit_to_core(packet, "abc", core)
                self.assertEqual(core.state_version, 0)


if __name__ == "__main__":
    unittest.main()
