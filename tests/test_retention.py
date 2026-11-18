import json
import tempfile
import unittest
from pathlib import Path

from surge_adapter.retention import (
    build_retention_manifest, create_payload_tombstone,
    load_retention_manifest, verify_retention_manifest, write_retention_manifest,
)


NOW = "2026-11-18T12:00:00Z"


class RetentionManifestTests(unittest.TestCase):
    def records(self):
        return [
            {"record_id": "low", "last_accessed_at": "2026-09-01T00:00:00Z",
             "salience": 0.1, "utility": 0.1, "interest": 0.0, "dependency": 0.0,
             "source_digest": "sha-low", "provenance_refs": ["p1"]},
            {"record_id": "high", "last_accessed_at": NOW,
             "salience": 0.95, "utility": 0.9, "interest": 0.8, "dependency": 0.7,
             "source_digest": "sha-high", "lineage_refs": ["parent"]},
            {"record_id": "medium", "last_accessed_at": "2026-11-01T00:00:00Z",
             "salience": 0.5, "utility": 0.5, "interest": 0.5, "dependency": 0.5},
        ]

    def test_capacity_plan_keeps_high_salience_and_emits_factors(self):
        manifest = build_retention_manifest(
            self.records(), capacity=1, policy_version="retention-v1", now=NOW)
        states = {item["record_id"]: item["state"] for item in manifest["records"]}
        self.assertEqual(states["high"], "ACTIVE")
        self.assertEqual(states["low"], "ARCHIVED")
        self.assertIn("salience", manifest["records"][0]["factors"])
        self.assertFalse(manifest["deletion_performed"])
        self.assertTrue(verify_retention_manifest(manifest))

    def test_coherence_is_not_an_eviction_factor(self):
        first = self.records()
        second = [{**item, "coherence": 0.0 if item["record_id"] == "high" else 1.0}
                  for item in first]
        a = build_retention_manifest(first, capacity=2, policy_version="p1", now=NOW)
        b = build_retention_manifest(second, capacity=2, policy_version="p1", now=NOW)
        self.assertEqual(a["ranking"], b["ranking"])

    def test_deterministic_output_and_stable_tie_break(self):
        data = [
            {"record_id": "b", "last_accessed_at": NOW},
            {"record_id": "a", "last_accessed_at": NOW},
        ]
        one = build_retention_manifest(data, capacity=1, policy_version="p1", now=NOW)
        two = build_retention_manifest(list(reversed(data)), capacity=1, policy_version="p1", now=NOW)
        self.assertEqual(one, two)
        self.assertEqual(one["ranking"], ["a", "b"])

    def test_manifest_tampering_is_rejected(self):
        manifest = build_retention_manifest(self.records(), capacity=2,
                                            policy_version="p1", now=NOW)
        next(item for item in manifest["records"] if item["state"] == "ARCHIVED")["state"] = "ACTIVE"
        with self.assertRaisesRegex(ValueError, "DIGEST_MISMATCH"):
            verify_retention_manifest(manifest)

    def test_tombstone_requires_archived_record_and_is_not_physical_delete(self):
        manifest = build_retention_manifest(self.records(), capacity=1,
                                            policy_version="p1", now=NOW)
        event = create_payload_tombstone(
            "low", manifest=manifest, reason="approved retention expiry",
            actor="retention-admin", policy_version="tombstone-v1", created_at=NOW)
        self.assertFalse(event["physical_deletion_performed"])
        self.assertEqual(event["manifest_sha256"], manifest["manifest_sha256"])
        with self.assertRaisesRegex(ValueError, "REQUIRES_ARCHIVED"):
            create_payload_tombstone(
                "high", manifest=manifest, reason="x", actor="a",
                policy_version="p1", created_at=NOW)

    def test_manifest_round_trip_is_atomic_and_verified(self):
        manifest = build_retention_manifest(self.records(), capacity=2,
                                            policy_version="p1", now=NOW)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "retention.json"
            write_retention_manifest(path, manifest)
            self.assertEqual(load_retention_manifest(path), manifest)

    def test_invalid_capacity_and_weights_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "CAPACITY_INVALID"):
            build_retention_manifest(self.records(), capacity=-1,
                                     policy_version="p1", now=NOW)
        with self.assertRaisesRegex(ValueError, "WEIGHTS_MUST_SUM"):
            build_retention_manifest(self.records(), capacity=1,
                                     policy_version="p1", now=NOW,
                                     weights={key: 0.1 for key in
                                              ("salience", "utility", "interest", "dependency", "recency")})

    def test_invalid_score_and_naive_time_fail_closed(self):
        bad = [{"record_id": "x", "last_accessed_at": NOW, "salience": float("nan")}]
        with self.assertRaisesRegex(ValueError, "OUT_OF_RANGE"):
            build_retention_manifest(bad, capacity=1, policy_version="p1", now=NOW)
        with self.assertRaisesRegex(ValueError, "MUST_BE_AWARE"):
            build_retention_manifest([{"record_id": "x", "last_accessed_at": "2026-11-18T12:00:00"}],
                                     capacity=1, policy_version="p1", now=NOW)


if __name__ == "__main__":
    unittest.main()
