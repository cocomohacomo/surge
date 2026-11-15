import hashlib
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from surge_adapter.packet_ingest import PacketRejected, RecognitionPacketAdapter, normalize_source


def packet_for(source, packet_id="p1", sequence=1, idem="i1"):
    raw = normalize_source(source)
    return {
        "schema_version": "0.9.0", "packet_id": packet_id,
        "idempotency_key": idem, "context_id": "ctx", "source_id": "doc",
        "source_position": {
            "block_ref": "b1", "start_offset": 0, "end_offset": len(raw),
            "offset_basis": "UTF8_BYTE_NFC", "source_order": sequence,
            "source_generation": "g1", "source_sequence": sequence,
            "source_range_digest": hashlib.sha256(raw).hexdigest()
        },
        "structural": {"block_type": "PARAGRAPH", "semantic_role": "MAIN_CONTENT"},
        "content": {"text": source, "normalized_digest": hashlib.sha256(raw).hexdigest(),
                    "language": "ja"},
        "context_hint": {"state_version": 0, "temporal_mode": "CURRENT",
                         "retrieval_reason": "initial"},
        "coverage": {"completeness": "COMPLETE"},
        "adapter": {"adapter_id": "test", "adapter_version": "1",
                    "capability_descriptor_ref": "cap-1"},
        "finalization": "FINAL"
    }


class PacketAdapterTests(unittest.TestCase):
    def test_valid_packet_and_duplicate_retry(self):
        adapter = RecognitionPacketAdapter()
        packet = packet_for("日本語")
        first = adapter.admit(packet, "日本語")
        second = adapter.admit(packet, "日本語")
        self.assertEqual(first, second)
        self.assertEqual(first.status, "ADMITTED")

    def test_nfc_byte_digest(self):
        adapter = RecognitionPacketAdapter()
        packet = packet_for("が")
        self.assertEqual(adapter.admit(packet, "か\u3099").range_digest,
                         hashlib.sha256("が".encode()).hexdigest())

    def test_range_digest_tamper_rejected(self):
        adapter = RecognitionPacketAdapter()
        packet = packet_for("abc")
        packet["source_position"]["source_range_digest"] = "0" * 64
        with self.assertRaisesRegex(PacketRejected, "SOURCE_RANGE_DIGEST_MISMATCH"):
            adapter.admit(packet, "abc")

    def test_snapshot_digest_tamper_rejected(self):
        adapter = RecognitionPacketAdapter()
        packet = packet_for("abc")
        packet["content"]["normalized_digest"] = "0" * 64
        with self.assertRaisesRegex(PacketRejected, "SOURCE_SNAPSHOT_DIGEST_MISMATCH"):
            adapter.admit(packet, "abc")

    def test_schema_rejects_unknown_field(self):
        adapter = RecognitionPacketAdapter()
        packet = packet_for("abc")
        packet["unexpected"] = True
        with self.assertRaisesRegex(PacketRejected, "SCHEMA_INVALID"):
            adapter.admit(packet, "abc")

    def test_idempotency_key_reuse_rejected(self):
        adapter = RecognitionPacketAdapter()
        adapter.admit(packet_for("abc"), "abc")
        changed = packet_for("abc", packet_id="p2", sequence=2, idem="i1")
        with self.assertRaisesRegex(PacketRejected, "IDEMPOTENCY_KEY_REUSED"):
            adapter.admit(changed, "abc")

    def test_sequence_reuse_rejected(self):
        adapter = RecognitionPacketAdapter()
        adapter.admit(packet_for("abc"), "abc")
        changed = packet_for("abc", packet_id="p2", sequence=1, idem="i2")
        with self.assertRaisesRegex(PacketRejected, "SOURCE_SEQUENCE_REUSED"):
            adapter.admit(changed, "abc")

    def test_revision_requires_lineage(self):
        adapter = RecognitionPacketAdapter()
        packet = packet_for("abc")
        packet["finalization"] = "REVISION"
        with self.assertRaisesRegex(PacketRejected, "REVISION_LINEAGE_REQUIRED"):
            adapter.admit(packet, "abc")


    def test_changed_range_content_conflicts(self):
        adapter = RecognitionPacketAdapter()
        adapter.admit(packet_for("abc"), "abc")
        changed = packet_for("abd", packet_id="p2", sequence=2, idem="i2")
        with self.assertRaisesRegex(PacketRejected, "RANGE_CONTENT_CONFLICT"):
            adapter.admit(changed, "abd")

    def test_revision_is_append_only_and_chained(self):
        adapter = RecognitionPacketAdapter()
        original = packet_for("abc")
        adapter.admit(original, "abc")
        revision = packet_for("abd", packet_id="p2", sequence=2, idem="i2")
        revision["finalization"] = "REVISION"
        revision["source_position"]["source_revision_sequence"] = 1
        revision["source_position"]["revision_of_ref"] = "p1"
        self.assertEqual(adapter.admit(revision, "abd").status, "REVISION_ADMITTED")
        revision2 = packet_for("abe", packet_id="p3", sequence=3, idem="i3")
        revision2["finalization"] = "REVISION"
        revision2["source_position"]["source_revision_sequence"] = 2
        revision2["source_position"]["revision_of_ref"] = "p2"
        self.assertEqual(adapter.admit(revision2, "abe").status, "REVISION_ADMITTED")


    def test_packet_id_reuse_rejected_without_mutating_existing_admission(self):
        adapter = RecognitionPacketAdapter()
        original = packet_for("abc")
        first = adapter.admit(original, "abc")
        changed = packet_for("xyz", packet_id="p1", sequence=2, idem="i2")
        with self.assertRaisesRegex(PacketRejected, "PACKET_ID_REUSED"):
            adapter.admit(changed, "xyz")
        self.assertEqual(adapter.admit(original, "abc"), first)

    def test_out_of_bounds_range_rejected(self):
        adapter = RecognitionPacketAdapter()
        packet = packet_for("abc")
        packet["source_position"]["end_offset"] = 100
        with self.assertRaisesRegex(PacketRejected, "SOURCE_RANGE_OUT_OF_BOUNDS"):
            adapter.admit(packet, "abc")


if __name__ == "__main__":
    unittest.main()
