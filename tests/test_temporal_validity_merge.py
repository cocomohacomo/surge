import unittest

from surge_adapter.temporal_validity_merge import propose_temporal_validity_merge


class TemporalValidityMergeTests(unittest.TestCase):
    def test_start_boundary_preserves_existing_end(self):
        result = propose_temporal_validity_merge(
            {"revision": 2, "valid_from": "2026-01-01T00:00:00+09:00",
             "valid_until": "2026-05-01T00:00:00+09:00"},
            [{"role": "VALID_FROM", "status": "RESOLVED",
              "valid_from": "2026-02-01T00:00:00+09:00", "valid_until": None}])
        self.assertEqual(result["status"], "MERGED_PROPOSAL")
        self.assertEqual(result["valid_until"], "2026-05-01T00:00:00+09:00")
        self.assertEqual(result["proposed_revision"], 3)
        self.assertFalse(result["claim_validity_write"])

    def test_end_boundary_preserves_existing_start(self):
        result = propose_temporal_validity_merge(
            {"revision": 1, "valid_from": "2026-01-01T00:00:00+09:00",
             "valid_until": None},
            [{"role": "VALID_UNTIL", "status": "RESOLVED",
              "valid_from": None, "valid_until": "2026-04-01T00:00:00+09:00"}])
        self.assertEqual(result["status"], "MERGED_PROPOSAL")
        self.assertEqual(result["valid_from"], "2026-01-01T00:00:00+09:00")

    def test_missing_opposite_boundary_is_not_commit_ready(self):
        result = propose_temporal_validity_merge(
            None, [{"role": "VALID_UNTIL", "status": "RESOLVED",
                    "valid_from": None, "valid_until": "2026-04-01T00:00:00+09:00"}])
        self.assertEqual(result["status"], "NEEDS_OTHER_BOUNDARY")
        self.assertFalse(result["claim_validity_write"])

    def test_multiple_candidates_are_never_ranked(self):
        candidate = {"role": "VALID_FROM", "status": "RESOLVED",
                     "valid_from": "2026-02-01T00:00:00+09:00", "valid_until": None}
        result = propose_temporal_validity_merge(None, [candidate, candidate])
        self.assertEqual(result["status"], "AMBIGUOUS_CANDIDATES")

    def test_timezone_free_boundary_is_rejected(self):
        result = propose_temporal_validity_merge(
            None, [{"role": "VALID_FROM", "status": "RESOLVED",
                    "valid_from": "2026-02-01T00:00:00", "valid_until": None}])
        self.assertTrue(result["status"].startswith("TEMPORAL_BOUNDARY_TIMEZONE_REQUIRED"))

    def test_reversed_interval_is_rejected(self):
        result = propose_temporal_validity_merge(
            {"revision": 0, "valid_from": "2026-05-01T00:00:00+09:00",
             "valid_until": "2026-06-01T00:00:00+09:00"},
            [{"role": "VALID_FROM", "status": "RESOLVED",
              "valid_from": "2026-07-01T00:00:00+09:00", "valid_until": None}])
        self.assertEqual(result["status"], "INTERVAL_INVALID")


if __name__ == "__main__":
    unittest.main()
