import unittest

from tools.evidence_resolver import resolve_evidence_offsets


class EvidenceResolverTests(unittest.TestCase):
    def make_prediction(self, span):
        return {"schema_version": "surge-projector-output-v1.0",
                "attributes": [{"key": "x", "value": "y", "polarity": "positive",
                               "category": "test", "evidence_spans": [span]}],
                "relations": [], "phenomena": [], "abstentions": []}

    def test_repairs_unicode_offsets_by_exact_unique_quote(self):
        case = {"text": "うん。", "context": ""}
        prediction = self.make_prediction({"source": "text", "start": 0, "end": 1, "quote": "うん。"})
        result = resolve_evidence_offsets(prediction, case)
        self.assertEqual(result["repaired"], 1)
        self.assertEqual(prediction["attributes"][0]["evidence_spans"][0]["start"], 0)
        self.assertEqual(prediction["attributes"][0]["evidence_spans"][0]["end"], 3)

    def test_keeps_valid_offsets_unchanged(self):
        case = {"text": "今日は雨です。", "context": ""}
        span = {"source": "text", "start": 2, "end": 4, "quote": "雨で"}
        prediction = self.make_prediction(span)
        result = resolve_evidence_offsets(prediction, case)
        self.assertEqual(result["repaired"], 0)
        self.assertEqual(span["start"], 2)
        self.assertEqual(span["end"], 4)

    def test_does_not_guess_duplicate_quote(self):
        case = {"text": "はい、はい", "context": ""}
        prediction = self.make_prediction({"source": "text", "start": 0, "end": 1, "quote": "はい"})
        result = resolve_evidence_offsets(prediction, case)
        self.assertEqual(result["repaired"], 0)
        self.assertEqual(result["unresolved"][0]["reason"], "quote_ambiguous")

    def test_reports_missing_quote(self):
        case = {"text": "今日は晴れ。", "context": ""}
        prediction = self.make_prediction({"source": "text", "start": 0, "end": 1, "quote": "雨"})
        result = resolve_evidence_offsets(prediction, case)
        self.assertEqual(result["repaired"], 0)
        self.assertEqual(result["unresolved"][0]["reason"], "quote_not_found")

    def test_ignores_non_object_entries_safely(self):
        prediction = {"attributes": [None], "relations": [], "phenomena": [], "abstentions": []}
        result = resolve_evidence_offsets(prediction, {"text": "", "context": ""})
        self.assertEqual(result["repaired"], 0)


if __name__ == "__main__":
    unittest.main()
