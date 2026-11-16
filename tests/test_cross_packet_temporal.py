import unittest

from surge_adapter.cross_packet_temporal import CrossPacketTemporalAccumulator


class CrossPacketTemporalAccumulatorTests(unittest.TestCase):
    def setUp(self):
        self.acc = CrossPacketTemporalAccumulator()

    def test_complements_start_and_end_across_packets_for_same_claim(self):
        first = self.acc.add("claim/A", {
            "role": "VALID_FROM", "status": "RESOLVED",
            "valid_from": "2026-04-01T00:00:00+09:00", "valid_until": None,
        }, evidence_id="evidence/1", packet_id="packet/1")
        self.assertEqual(first["status"], "NEEDS_OTHER_BOUNDARY")
        second = self.acc.add("claim/A", {
            "role": "VALID_UNTIL", "status": "RESOLVED",
            "valid_from": None, "valid_until": "2026-04-01T00:00:00+09:00",
        }, evidence_id="evidence/2", packet_id="packet/2")
        self.assertEqual(second["status"], "MERGED_PROPOSAL_PENDING_REVIEW")
        self.assertEqual(second["basis_evidence_ids"], ["evidence/1", "evidence/2"])
        self.assertEqual(second["source_packet_ids"], ["packet/1", "packet/2"])
        self.assertFalse(second["claim_validity_write"])

    def test_never_crosses_claim_identity(self):
        self.acc.add("claim/A", {
            "role": "VALID_FROM", "status": "RESOLVED",
            "valid_from": "2026-04-01T00:00:00+09:00", "valid_until": None,
        }, evidence_id="e1", packet_id="p1")
        other = self.acc.add("claim/B", {
            "role": "VALID_UNTIL", "status": "RESOLVED",
            "valid_from": None, "valid_until": "2026-05-01T00:00:00+09:00",
        }, evidence_id="e2", packet_id="p2")
        self.assertEqual(other["status"], "NEEDS_OTHER_BOUNDARY")

    def test_conflicting_same_role_requires_review(self):
        self.acc.add("claim/A", {
            "role": "VALID_FROM", "status": "RESOLVED",
            "valid_from": "2026-04-01T00:00:00+09:00", "valid_until": None,
        }, evidence_id="e1", packet_id="p1")
        result = self.acc.add("claim/A", {
            "role": "VALID_FROM", "status": "RESOLVED",
            "valid_from": "2026-05-01T00:00:00+09:00", "valid_until": None,
        }, evidence_id="e2", packet_id="p2")
        self.assertEqual(result["status"], "BOUNDARY_CONFLICT_REVIEW_REQUIRED")

    def test_unresolved_and_missing_provenance_rejected(self):
        result = self.acc.add("claim/A", {"role": "VALID_FROM", "status": "NEEDS_YEAR"},
                              evidence_id="e1", packet_id="p1")
        self.assertEqual(result["status"], "CANDIDATE_NOT_RESOLVED")
        result = self.acc.add("claim/A", {"role": "VALID_FROM", "status": "RESOLVED",
                              "valid_from": "2026-04-01T00:00:00+09:00"},
                              evidence_id="", packet_id="p1")
        self.assertEqual(result["status"], "PROVENANCE_REQUIRED")

    def test_capacity_is_bounded_and_explicit_clear(self):
        acc = CrossPacketTemporalAccumulator(max_claims=1)
        acc.add("claim/A", {"role": "VALID_FROM", "status": "RESOLVED",
             "valid_from": "2026-04-01T00:00:00+09:00"}, evidence_id="e1", packet_id="p1")
        result = acc.add("claim/B", {"role": "VALID_FROM", "status": "RESOLVED",
             "valid_from": "2026-04-01T00:00:00+09:00"}, evidence_id="e2", packet_id="p2")
        self.assertEqual(result["status"], "ACCUMULATOR_CAPACITY_EXCEEDED")
        self.assertTrue(acc.clear("claim/A"))
        self.assertEqual(acc.pending_claim_count(), 0)


if __name__ == "__main__":
    unittest.main()
