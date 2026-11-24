import unittest
from tools.evaluate_projector_semantics import evaluate_case, evaluate_report

class ProjectorSemanticEvaluationTests(unittest.TestCase):
    def test_json_validity_does_not_imply_semantic_coverage(self):
        result = evaluate_case({
            "case_id": "contradiction",
            "parsed_output": {"candidates": [{"text": "締切は水曜日"}], "relations": []},
            "expected_candidate_markers": ["水曜日"],
            "expected_relation_types": ["contradicts"],
        })
        self.assertTrue(result["json_valid"])
        self.assertEqual(result["semantic_feature_hits"], 1)
        self.assertEqual(result["semantic_feature_total"], 2)
        self.assertEqual(result["semantic_feature_recall"], 0.5)

    def test_relation_type_is_evaluated_separately(self):
        result = evaluate_case({
            "case_id": "exception",
            "parsed_output": {
                "candidates": [{"text": "災害時は例外"}],
                "relations": [{"type": "exception_of"}],
            },
            "expected_candidate_markers": ["災害", "例外"],
            "expected_relation_types": ["exception_of"],
        })
        self.assertEqual(result["semantic_feature_hits"], 3)
        self.assertEqual(result["semantic_feature_recall"], 1.0)

    def test_missing_or_invalid_json_fails_semantic_gate(self):
        result = evaluate_report({"models": [{
            "name": "fixture", "results": [{
                "case_id": "ack", "parsed_output": None,
                "expected_candidate_markers": ["承諾"],
                "expected_relation_types": [],
            }]
        }]})
        self.assertEqual(result["models"][0]["gate"], "FAIL")
        self.assertEqual(result["models"][0]["json_valid_cases"], 0)

if __name__ == "__main__":
    unittest.main()
