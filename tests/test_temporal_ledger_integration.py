import unittest
from surge_adapter.temporal_ledger_integration import persist_temporal_binding

class FakeCore:
    state_version = 7
    def __init__(self): self.calls = []
    def add_temporal_boundary(self, **kwargs):
        self.calls.append(kwargs)
        return {"committed": True, "duplicate": False, "state_version": 8,
                "status": "NEEDS_OTHER_BOUNDARY", "pending": {"claim_id": kwargs["claim_id"]}}

class TemporalLedgerIntegrationTests(unittest.TestCase):
    def binding(self):
        return {"binding_status": "BOUND_PENDING_CORE_REVIEW",
                "claim_ref": "claim/p1", "evidence_ref": "evidence/p1",
                "temporal_candidate": {"status": "RESOLVED", "role": "VALID_FROM",
                    "valid_from": "2026-04-01T00:00:00+09:00", "valid_until": None,
                    "correction": False},
                "authority": "PROPOSAL_ONLY", "claim_validity_write": False}
    def test_persists_explicit_provenance_and_deterministic_ids(self):
        core = FakeCore()
        result = persist_temporal_binding(core, self.binding(), packet_id="p1")
        self.assertTrue(result["persisted"])
        self.assertFalse(result["claim_validity_write"])
        self.assertEqual(core.calls[0]["claim_id"], "claim/p1")
        self.assertEqual(core.calls[0]["evidence_id"], "evidence/p1")
        self.assertEqual(core.calls[0]["packet_id"], "p1")
        self.assertEqual(core.calls[0]["expected_state_version"], 7)
        self.assertEqual(core.calls[0]["idempotency_key"], "temporal-idem/p1/VALID_FROM")
    def test_non_bound_or_unresolved_candidate_is_not_written(self):
        core = FakeCore()
        binding = self.binding()
        binding["binding_status"] = "MULTIPLE_TEMPORAL_CUES"
        self.assertFalse(persist_temporal_binding(core, binding, packet_id="p1")["persisted"])
        self.assertEqual(core.calls, [])
        binding = self.binding()
        binding["temporal_candidate"]["status"] = "NEEDS_TIMEZONE"
        self.assertFalse(persist_temporal_binding(core, binding, packet_id="p1")["persisted"])
        self.assertEqual(core.calls, [])
    def test_core_rejection_is_reported_without_raising_or_claim_write(self):
        class RejectingCore(FakeCore):
            def add_temporal_boundary(self, **kwargs):
                raise ValueError("BOUNDARY_CONFLICT_REVIEW_REQUIRED")
        core = RejectingCore()
        result = persist_temporal_binding(core, self.binding(), packet_id="p1")
        self.assertFalse(result["persisted"])
        self.assertTrue(result["requires_review"])
        self.assertEqual(result["status"], "BOUNDARY_CONFLICT_REVIEW_REQUIRED")
        self.assertFalse(result["claim_validity_write")

    def test_authority_or_role_mismatch_is_rejected(self):
        core = FakeCore()
        binding = self.binding()
        binding["claim_validity_write"] = True
        self.assertEqual(persist_temporal_binding(core, binding, packet_id="p1")["status"],
                         "BINDING_AUTHORITY_INVALID")
        binding = self.binding()
        binding["temporal_candidate"]["role"] = "UNSUPPORTED"
        self.assertEqual(persist_temporal_binding(core, binding, packet_id="p1")["status"],
                         "UNSUPPORTED_TEMPORAL_ROLE")
        self.assertEqual(core.calls, [])

if __name__ == "__main__": unittest.main()
