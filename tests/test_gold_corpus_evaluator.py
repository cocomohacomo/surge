import unittest
from tools.evaluate_gold_corpus import evaluate_case, evaluate


class GoldCorpusEvaluatorTests(unittest.TestCase):
    def test_attribute_relation_and_evidence_exact_match(self):
        case = {"case_id": "x", "text": "締切は水曜日", "gold": {
            "attributes": [{"key": "deadline", "value": "水曜日", "polarity": "positive"}],
            "relations": [{"type": "supersedes", "source": "new", "target": "old"}]}}
        pred = {"attributes": [{"key": "deadline", "value": "水曜日",
                                "polarity": "positive", "evidence_quote": "締切は水曜日"}],
                "relations": [{"type": "supersedes", "source": "new", "target": "old"}]}
        result = evaluate_case(case, pred)
        self.assertEqual(result["attribute_metrics"]["f1"], 1.0)
        self.assertEqual(result["relation_metrics"]["f1"], 1.0)
        self.assertEqual(result["evidence_support_rate"], 1.0)

    def test_extra_attribute_and_relation_are_false_positives(self):
        case = {"case_id": "x", "text": "A", "gold": {"attributes": [], "relations": []}}
        result = evaluate_case(case, {"attributes": [{"key": "x", "value": "invented"}],
                                      "relations": [{"type": "supports", "source": "x", "target": "y"}]})
        self.assertEqual(result["attribute_metrics"]["fp"], 1)
        self.assertEqual(result["relation_metrics"]["fp"], 1)

    def test_duplicate_outputs_count_as_false_positives(self):
        case = {"case_id": "dup", "text": "期限は水曜", "gold": {
            "attributes": [{"key": "期限", "value": "水曜"}], "relations": []}}
        pred = {"attributes": [
            {"key": "期限", "value": "水曜", "evidence_quote": "期限は水曜"},
            {"key": "期限", "value": "水曜", "evidence_quote": "期限は水曜"}], "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["duplicate_attribute_count"], 1)
        self.assertEqual(result["attribute"]["fp"], 1)

    def test_wrong_category_is_fp_and_gold_category_is_fn(self):
        case = {"case_id": "category", "categories": ["negation"], "text": "参加しない",
                "gold": {"attributes": [{"key": "参加", "value": "参加",
                                         "polarity": "negative", "category": "negation"}],
                         "relations": []}}
        pred = {"attributes": [{"key": "参加", "value": "参加", "polarity": "negative",
                                "category": "affirmation", "evidence_quote": "参加しない"}],
                "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["categories"]["negation"]["fn"], 1)
        self.assertEqual(result["categories"]["affirmation"]["fp"], 1)

    def test_missing_evidence_is_unsupported(self):
        case = {"case_id": "ev", "text": "期限は水曜",
                "gold": {"attributes": [{"key": "期限", "value": "水曜"}], "relations": []}}
        result = evaluate_case(case, {"attributes": [{"key": "期限", "value": "水曜"}],
                                      "relations": []})
        self.assertEqual(result["unsupported_attribute_count"], 1)

    def test_unadjudicated_seed_is_not_run(self):
        corpus = {"cases": [{"case_id": "seed", "split": "seed", "gold": {"attributes": [], "relations": []}}],
                  "annotation": {"independent_adjudication": False}, "profile": {}}
        self.assertEqual(evaluate(corpus, {}, "digest")["recognition_quality_gate"], "NOT_RUN")

    def test_missing_test_prediction_fails_closed(self):
        corpus = {"cases": [{"case_id": "test", "split": "test", "gold": {"attributes": [], "relations": []}}],
                  "annotation": {"independent_adjudication": True}, "profile": {}}
        self.assertEqual(evaluate(corpus, {}, "digest")["recognition_quality_gate"], "FAIL")


if __name__ == "__main__":
    unittest.main()
