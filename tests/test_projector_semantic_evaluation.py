import unittest

from tools.evaluate_projector_semantics import evaluate_case, evaluate_report


class ProjectorSemanticEvaluationTests(unittest.TestCase):
    def test_precision_recall_and_f1_penalise_extra_candidates(self):
        result = evaluate_case({
            "case_id": "contradiction",
            "parsed_output": {
                "candidates": [{"text": "締切は水曜日"}, {"text": "無関係な雑談"}],
                "relations": [{"type": "contradicts"}, {"type": "supports"}],
            },
            "expected_candidate_markers": ["水曜日"],
            "expected_relation_types": ["contradicts"],
        })
        self.assertEqual((result["tp"], result["fp"], result["fn"]), (2, 2, 0))
        self.assertEqual(result["precision"], 0.5)
        self.assertEqual(result["recall"], 1.0)
        self.assertAlmostEqual(result["f1"], 2 / 3)

    def test_negative_case_counts_all_unexpected_output_as_false_positive(self):
        result = evaluate_case({
            "case_id": "neutral_ack",
            "parsed_output": {
                "candidates": [{"text": "単なる相槌であり同意とは限らない"}],
                "relations": [{"type": "supports"}],
            },
            "expected_candidate_markers": [],
            "expected_relation_types": [],
        })
        self.assertEqual(result["tp"], 0)
        self.assertEqual(result["fp"], 2)
        self.assertEqual(result["fn"], 0)
        self.assertEqual(result["precision"], 0.0)
        self.assertEqual(result["recall"], 1.0)

    def test_duplicate_predictions_are_false_positives(self):
        result = evaluate_case({
            "case_id": "duplicate",
            "parsed_output": {
                "candidates": [{"text": "2027年4月1日"}, {"text": "2027年4月1日"}],
                "relations": [],
            },
            "expected_candidate_markers": ["2027年4月1日"],
            "expected_relation_types": [],
        })
        self.assertEqual((result["candidate_tp"], result["candidate_fp"]), (1, 1))

    def test_json_validity_does_not_imply_semantic_quality(self):
        report = evaluate_report({"models": [{
            "name": "fixture",
            "results": [{
                "case_id": "miss",
                "parsed_output": {"candidates": [], "relations": []},
                "expected_candidate_markers": ["承諾"],
                "expected_relation_types": [],
            }],
        }]})
        model = report["models"][0]
        self.assertEqual(model["json_valid_cases"], 1)
        self.assertEqual(model["recall"], 0.0)
        self.assertEqual(model["quality_gate"], "FAIL")

    def test_invalid_structure_is_rejected(self):
        with self.assertRaises(ValueError):
            evaluate_case({
                "case_id": "bad",
                "parsed_output": {"candidates": ["not-an-object"], "relations": []},
                "expected_candidate_markers": [],
                "expected_relation_types": [],
            })

    def test_empty_report_model_is_rejected(self):
        with self.assertRaises(ValueError):
            evaluate_report({"models": [{"name": "empty", "results": []}]})


if __name__ == "__main__":
    unittest.main()
