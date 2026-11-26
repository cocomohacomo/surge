import unittest
from tools.summarize_model_matrix import summarize


def valid_row():
    metric = {"tp": 1, "fp": 0, "fn": 0, "precision": 1.0, "recall": 1.0, "f1": 1.0}
    semantic = {"attribute_metrics": metric, "relation_metrics": metric,
                "unsupported_attribute_count": 0, "duplicate_attribute_count": 0,
                "duplicate_relation_count": 0, "malformed_count": 0}
    return {"json_valid": True, "timeout": False, "peak_rss_kib": 100,
            "engineering_semantic_metrics": semantic}


class ModelMatrixSummaryTests(unittest.TestCase):
    def test_invalid_json_fails(self):
        report = {"corpus_status": "SEED_NOT_INDEPENDENT",
                  "models": [{"name": "m", "results": [{"json_valid": False, "timeout": False}]}]}
        result = summarize(report)
        self.assertEqual(result["model_matrix_execution_gate"], "FAIL")
        self.assertEqual(result["recognition_quality_gate"], "NOT_RUN")

    def test_timeout_and_missing_rss_fail(self):
        report = {"models": [{"name": "m", "results": [{"json_valid": True, "timeout": True}]}]}
        self.assertEqual(summarize(report)["model_matrix_execution_gate"], "FAIL")

    def test_missing_semantic_metrics_fail(self):
        report = {"models": [{"name": "m", "results": [valid_row() | {"engineering_semantic_metrics": None}]}]}
        self.assertEqual(summarize(report)["model_matrix_execution_gate"], "FAIL")

    def test_empty_or_incomplete_model_matrix_fails(self):
        self.assertEqual(summarize({"models": []})["model_matrix_execution_gate"], "FAIL")
        report = {"expected_model_count": 2, "models": [{"name": "m", "results": [valid_row()]}]}
        self.assertEqual(summarize(report)["model_matrix_execution_gate"], "FAIL")

    def test_complete_model_matrix_passes_execution_only(self):
        report = {"corpus_status": "SEED_NOT_INDEPENDENT", "expected_model_count": 1,
                  "models": [{"name": "m", "results": [valid_row()]}]}
        result = summarize(report)
        self.assertEqual(result["model_matrix_execution_gate"], "PASS")
        self.assertEqual(result["recognition_quality_gate"], "NOT_RUN")


if __name__ == "__main__":
    unittest.main()
