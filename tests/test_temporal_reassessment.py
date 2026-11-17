import unittest

from surge_adapter.temporal_reassessment import (
    TemporalReassessmentLedger, assess_interval, evaluate_claim_reassessment,
)


class TemporalReassessmentTests(unittest.TestCase):
    def claim(self, **extra):
        value = {"claim_id": "claim/42", "revision": 3, "claim_status": "ACTIVE",
                 "valid_from": "2026-04-01T00:00:00+09:00",
                 "valid_until": "2027-04-01T00:00:00+09:00",
                 "recorded_state_version": 81}
        value.update(extra)
        return value

    def test_half_open_boundaries(self):
        self.assertEqual(assess_interval("2026-04-01T00:00:00+09:00",
            "2027-04-01T00:00:00+09:00", "2026-04-01T00:00:00+09:00").status, "VALID")
        self.assertEqual(assess_interval("2026-04-01T00:00:00+09:00",
            "2027-04-01T00:00:00+09:00", "2027-04-01T00:00:00+09:00").status, "EXPIRED")

    def test_missing_claim_interval_is_unknown_not_valid(self):
        result = evaluate_claim_reassessment(self.claim(valid_from=None), [], reference_at="2026-06-01T00:00:00+09:00")
        self.assertEqual(result["decision"], "CLAIM_NOT_CURRENTLY_VALID")
        self.assertEqual(result["claim_temporal"]["status"], "UNKNOWN")

    def test_support_requires_evidence_temporal_validity(self):
        evidence = [{"evidence_id": "ev/1", "relation": "SUPPORTS",
                     "valid_from": "2026-01-01T00:00:00+09:00",
                     "valid_until": "2026-05-01T00:00:00+09:00",
                     "observed_at": "2026-04-02T12:00:00+09:00"}]
        result = evaluate_claim_reassessment(self.claim(), evidence, reference_at="2026-06-01T00:00:00+09:00")
        self.assertEqual(result["decision"], "INSUFFICIENT_TEMPORAL_PROVENANCE")
        self.assertEqual(result["evidence"][0]["temporal_status"], "EXPIRED")
        self.assertEqual(result["evidence"][0]["observed_at"], "2026-04-02T03:00:00+00:00")

    def test_support_and_contradiction_create_explainable_conflict(self):
        evidence = [
            {"evidence_id": "ev/s", "relation": "SUPPORTS", "valid_from": "2026-01-01T00:00:00+09:00"},
            {"evidence_id": "ev/c", "relation": "CONTRADICTS", "valid_from": "2026-05-01T00:00:00+09:00"},
        ]
        result = evaluate_claim_reassessment(self.claim(), evidence, reference_at="2026-06-01T00:00:00+09:00")
        self.assertEqual(result["decision"], "TEMPORAL_CONFLICT")
        self.assertEqual(result["active_support_evidence_ids"], ["ev/s"])
        self.assertEqual(result["active_contradiction_evidence_ids"], ["ev/c"])

    def test_evidence_without_valid_from_is_unknown(self):
        result = evaluate_claim_reassessment(self.claim(),
            [{"evidence_id": "ev/u", "relation": "SUPPORTS"}],
            reference_at="2026-06-01T00:00:00+09:00")
        self.assertEqual(result["decision"], "INSUFFICIENT_TEMPORAL_PROVENANCE")
        self.assertEqual(result["evidence"][0]["temporal_status"], "UNKNOWN")

    def test_inactive_claim_is_not_current_even_if_interval_valid(self):
        result = evaluate_claim_reassessment(self.claim(claim_status="SUPERSEDED"), [
            {"evidence_id": "ev/1", "relation": "SUPPORTS", "valid_from": "2026-01-01T00:00:00+09:00"}
        ], reference_at="2026-06-01T00:00:00+09:00")
        self.assertEqual(result["decision"], "CLAIM_NOT_ACTIVE")

    def test_timezone_and_interval_errors_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "TIMEZONE_REQUIRED"):
            evaluate_claim_reassessment(self.claim(), [], reference_at="2026-06-01T00:00:00")
        with self.assertRaisesRegex(ValueError, "INTERVAL_INVALID"):
            assess_interval("2026-06-02T00:00:00+09:00", "2026-06-01T00:00:00+09:00",
                            "2026-06-01T00:00:00+09:00")

    def test_trace_ledger_is_append_only_and_replay_verifiable(self):
        trace = evaluate_claim_reassessment(self.claim(), [
            {"evidence_id": "ev/1", "relation": "SUPPORTS", "valid_from": "2026-01-01T00:00:00+09:00"}
        ], reference_at="2026-06-01T00:00:00+09:00")
        ledger = TemporalReassessmentLedger()
        first = ledger.append(trace)
        second = ledger.append(trace)
        self.assertEqual((first["sequence"], second["sequence"]), (1, 2))
        replayed = TemporalReassessmentLedger.replay(ledger.records())
        self.assertEqual(replayed.records(), ledger.records())
        tampered = ledger.records()
        tampered[0]["evidence"][0]["valid_from"] = "2020-01-01T00:00:00+09:00"
        with self.assertRaisesRegex(ValueError, "CHAIN_HASH_INVALID"):
            TemporalReassessmentLedger.replay(tampered)

    def test_trace_is_not_a_core_write(self):
        trace = evaluate_claim_reassessment(self.claim(), [], reference_at="2026-06-01T00:00:00+09:00")
        self.assertFalse(trace["core_mutation"])
        ledger = TemporalReassessmentLedger()
        trace["core_mutation"] = True
        with self.assertRaisesRegex(ValueError, "MUST_BE_READ_ONLY"):
            ledger.append(trace)


if __name__ == "__main__":
    unittest.main()
