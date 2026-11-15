import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
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
