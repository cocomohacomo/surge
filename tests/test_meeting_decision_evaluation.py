import hashlib
import unittest

from examples.meeting_decisions.demo_adapter import predict
from tools.evaluate_meeting_decisions import evaluate
from tools.run_meeting_decision_adapter import run


class MeetingDecisionEvaluationTests(unittest.TestCase):
    def setUp(self):
        self.source = "会議では予算案を承認し、田中さんが12月末までに見積書を提出する。"
        self.digest = hashlib.sha256(self.source.encode("utf-8")).hexdigest()
        self.quote = "予算案を承認"
        self.start = self.source.encode("utf-8").index(self.quote.encode("utf-8"))
        self.end = self.start + len(self.quote.encode("utf-8"))
        self.cases = [{"case_id": "m1", "source_text": self.source, "source_sha256": self.digest}]
        self.gold = [{"case_id": "m1", "source_sha256": self.digest, "items": [{
            "action": "予算案を承認", "owner": "田中", "deadline": "2026-12-31",
            "critical": True, "evidence_quote": self.quote,
            "evidence_start": self.start, "evidence_end": self.end}]}]
        self.pred = [{"case_id": "m1", "source_sha256": self.digest, "decision": "ACCEPT", "items": [{
            "action": "予算案を承認", "owner": "田中", "deadline": "2026-12-31",
            "evidence_quote": self.quote, "evidence_start": self.start, "evidence_end": self.end,
            "source_sha256": self.digest}]}]

    def test_exact_grounded_prediction_passes(self):
        result = evaluate(self.cases, self.gold, self.pred, 1.0, 1.0, 0.0)
        self.assertEqual(result["gate"], "PASS")
        self.assertEqual(result["micro"]["f1"], 1.0)
        self.assertEqual(result["evidence_grounding_rate"], 1.0)

    def test_abstention_reduces_coverage_and_is_not_correct(self):
        self.pred[0]["decision"] = "ABSTAIN"
        result = evaluate(self.cases, self.gold, self.pred, 0.7, 0.9, 0.1)
        self.assertEqual(result["gate"], "INCONCLUSIVE")
        self.assertEqual(result["coverage"], 0.0)
        self.assertIsNone(result["selective_risk"])

    def test_bad_evidence_span_is_not_grounded(self):
        self.pred[0]["items"][0]["evidence_end"] += 1
        result = evaluate(self.cases, self.gold, self.pred, 0.0, 0.0, 1.0)
        self.assertEqual(result["evidence_grounding_rate"], 0.0)
        self.assertEqual(result["gate"], "FAIL")

    def test_source_digest_mismatch_is_rejected(self):
        self.pred[0]["source_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SOURCE_DIGEST_MISMATCH"):
            evaluate(self.cases, self.gold, self.pred, 0.7, 0.9, 0.1)

    def test_demo_adapter_emits_exact_utf8_evidence(self):
        source = "決定: 予算案を承認\n担当: 田中\n期限: 2026-12-31"
        digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
        case = {"case_id": "demo", "source_text": source, "source_sha256": digest}
        outputs = run([case], predict)
        item = outputs[0]["items"][0]
        self.assertEqual(outputs[0]["decision"], "ACCEPT")
        self.assertEqual(source.encode("utf-8")[item["evidence_start"]:item["evidence_end"]].decode(), item["evidence_quote"])
        self.assertEqual(item["source_sha256"], digest)


if __name__ == "__main__":
    unittest.main()
