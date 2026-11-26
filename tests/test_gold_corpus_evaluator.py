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

    def test_empty_gold_and_empty_prediction_never_score_perfect(self):
        result = evaluate_case({"case_id": "empty", "text": "x",
                                "gold": {"attributes": [], "relations": []}},
                               {"attributes": [], "relations": []})
        self.assertEqual(result["attribute_metrics"]["precision"], 0.0)
        self.assertEqual(result["attribute_metrics"]["recall"], 0.0)

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
        self.assertEqual(result["attribute_metrics"]["fp"], 1)

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

    def test_missing_category_is_not_silently_dropped(self):
        case = {"case_id": "category", "text": "参加する",
                "gold": {"attributes": [{"key": "参加", "value": "参加", "category": "affirmation"}],
                         "relations": []}}
        result = evaluate_case(case, {"attributes": [{"key": "参加", "value": "参加"}],
                                      "relations": []})
        self.assertEqual(result["categories"]["affirmation"]["fn"], 1)
        self.assertEqual(result["categories"]["__missing_category__"]["fp"], 1)

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

    def test_extra_prediction_case_fails_closed(self):
        corpus = {"cases": [{"case_id": "test", "split": "test", "gold": {"attributes": [], "relations": []}}],
                  "annotation": {"independent_adjudication": True}, "profile": {}}
        result = evaluate(corpus, {"test": {"attributes": [], "relations": []},
                                   "unexpected": {"attributes": [], "relations": []}}, "digest")
        self.assertEqual(result["recognition_quality_gate"], "FAIL")
        self.assertEqual(result["extra_case_ids"], ["unexpected"])


    def test_legacy_candidates_alias_is_not_canonical_schema(self):
        case = {"case_id": "schema", "text": "x",
                "gold": {"attributes": [], "relations": []}}
        result = evaluate_case(case, {"candidates": [], "relations": []})
        self.assertFalse(result["schema_valid"])
        self.assertEqual(result["malformed_count"], 1)


    def test_exact_evidence_span_precision_recall(self):
        span = {"source": "text", "start": 3, "end": 6, "quote": "水曜日"}
        case = {"case_id": "span", "text": "締切は水曜日",
                "gold": {"attributes": [{"key": "deadline", "value": "水曜日",
                                         "polarity": "positive", "category": "temporal",
                                         "evidence_spans": [span]}], "relations": []}}
        pred = {"attributes": [{"key": "deadline", "value": "水曜日",
                                "polarity": "positive", "category": "temporal",
                                "evidence_spans": [span]}], "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["evidence_span_metrics"]["precision"], 1.0)
        self.assertEqual(result["evidence_span_metrics"]["recall"], 1.0)

    def test_invalid_evidence_span_is_malformed_and_unsupported(self):
        case = {"case_id": "span-invalid", "text": "期限は水曜",
                "gold": {"attributes": [{"key": "期限", "value": "水曜"}], "relations": []}}
        pred = {"attributes": [{"key": "期限", "value": "水曜",
                                "evidence_spans": [{"source": "text", "start": 0,
                                                    "end": 2, "quote": "誤った引用"}]}],
                "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["malformed_count"], 1)
        self.assertEqual(result["unsupported_attribute_count"], 1)

    def test_span_evaluation_requires_gold_spans_and_threshold(self):
        corpus = {"evidence_span_evaluation": True,
                  "cases": [{"case_id": "span-profile", "split": "test", "text": "期限は水曜",
                             "gold": {"attributes": [{"key": "期限", "value": "水曜"}],
                                      "relations": []}}],
                  "annotation": {"independent_adjudication": True},
                  "profile": {"attribute_f1_min": 0, "relation_f1_min": 0,
                              "macro_f1_min": 0, "evidence_support_min": 0,
                              "schema_valid_rate_min": 0,
                              "false_positive_relation_rate_max": 1,
                              "category_recall_min": {"temporal": 0}}}
        pred = {"span-profile": {"attributes": [{"key": "期限", "value": "水曜"}],
                                 "relations": []}}
        result = evaluate(corpus, pred, "corpus")
        self.assertIn("evidence_span_gold_empty", result["failures"])


    def test_evaluator_schema_rate_uses_canonical_schema(self):
        case = {"case_id": "canonical", "text": "期限は水曜",
                "gold": {"attributes": [{"key": "期限", "value": "水曜"}],
                         "relations": []}}
        pred = {"schema_version": "surge-projector-output-v1.0",
                "attributes": [{"key": "期限", "value": "水曜",
                                "polarity": "positive", "category": "temporal",
                                "evidence_spans": [{"source": "text", "start": 3,
                                                    "end": 5, "quote": "水曜"}]}],
                "relations": []}
        result = evaluate_case(case, pred)
        self.assertTrue(result["schema_valid"])
        self.assertEqual(result["canonical_schema_error_count"], 0)

    def test_duplicate_corpus_case_id_rejected(self):
        case = {"case_id": "same", "split": "test", "gold": {"attributes": [], "relations": []}}
        with self.assertRaises(ValueError):
            evaluate({"cases": [case, case], "annotation": {"independent_adjudication": True}},
                     {"same": {"attributes": [], "relations": []}}, "digest")


if __name__ == "__main__":
    unittest.main()
