import unittest
from tools.summarize_model_matrix import summarize


def valid_row(case_id="case-1"):
    metric = {"tp": 1, "fp": 0, "fn": 0, "precision": 1.0, "recall": 1.0, "f1": 1.0}
    semantic = {"attribute_metrics": metric, "relation_metrics": metric,
                "unsupported_attribute_count": 0, "duplicate_attribute_count": 0,
                "duplicate_relation_count": 0, "duplicate_evidence_span_count": 0, "malformed_count": 0,
                "categories": {"negation": metric},
                "phenomenon_metrics": {"negation": metric, "contradiction": metric,
                                     "temporal_supersession": metric, "ambiguity": metric},
                "evidence_span_metrics": metric,
                "attribute_evidence_span_metrics": metric,
                "relation_evidence_span_metrics": metric}
    return {"case_id": case_id, "json_valid": True, "projector_schema_valid": True, "timeout": False,
            "return_code": 0, "peak_rss_kib": 100,
            "engineering_semantic_metrics": semantic}


def report(rows=None):
    return {"corpus_status": "SEED_NOT_INDEPENDENT", "expected_model_count": 1,
            "expected_case_ids": ["case-1"],
            "models": [{"name": "m", "results": rows if rows is not None else [valid_row()]}]}


class ModelMatrixSummaryTests(unittest.TestCase):
    def test_invalid_json_fails(self):
        row = valid_row() | {"json_valid": False}
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"], "FAIL")

    def test_timeout_and_missing_rss_fail(self):
        row = valid_row() | {"timeout": True}
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"], "FAIL")
        row = valid_row() | {"peak_rss_kib": None}
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"], "FAIL")

    def test_missing_semantic_metrics_fail(self):
        row = valid_row() | {"engineering_semantic_metrics": None}
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"], "FAIL")


    def test_invalid_projector_schema_fails(self):
        row = valid_row() | {"projector_schema_valid": False}
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"], "FAIL")

    def test_nonzero_exit_fails(self):
        self.assertEqual(summarize(report([valid_row() | {"return_code": 2}]))["model_matrix_execution_gate"], "FAIL")

    def test_execution_error_fails_even_with_valid_last_case_output(self):
        row = valid_row() | {"execution_error": "sampler_initialization_failed"}
        result = summarize(report([row]))
        self.assertEqual(result["model_matrix_execution_gate"], "FAIL")
        self.assertEqual(result["models"][0]["execution_errors"], 1)

    def test_missing_or_duplicate_case_coverage_fails(self):
        self.assertEqual(summarize(report([]))["model_matrix_execution_gate"], "FAIL")
        self.assertEqual(summarize(report([valid_row("unexpected")]))["model_matrix_execution_gate"], "FAIL")
        self.assertEqual(summarize(report([valid_row(), valid_row()]))["model_matrix_execution_gate"], "FAIL")

    def test_empty_or_incomplete_model_matrix_fails(self):
        self.assertEqual(summarize({"models": [], "expected_model_count": 1,
                                    "expected_case_ids": ["case-1"]})["model_matrix_execution_gate"], "FAIL")
        data = report()
        data["expected_model_count"] = 2
        self.assertEqual(summarize(data)["model_matrix_execution_gate"], "FAIL")




    def test_boolean_metric_and_counter_values_fail(self):
        row=valid_row()
        row["engineering_semantic_metrics"]["attribute_metrics"]["tp"]=True
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"],"FAIL")
        row=valid_row()
        row["engineering_semantic_metrics"]["unsupported_attribute_count"]=True
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"],"FAIL")

    def test_boolean_expected_model_count_fails(self):
        data=report()
        data["expected_model_count"]=True
        self.assertEqual(summarize(data)["model_matrix_execution_gate"],"FAIL")

    def test_missing_phenomenon_dimension_fails(self):
        row=valid_row()
        del row["engineering_semantic_metrics"]["phenomenon_metrics"]["ambiguity"]
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"],"FAIL")

    def test_non_finite_or_negative_metrics_fail(self):
        row = valid_row()
        row["engineering_semantic_metrics"]["attribute_metrics"]["f1"] = float("nan")
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"], "FAIL")
        row = valid_row()
        row["engineering_semantic_metrics"]["relation_metrics"]["tp"] = -1
        self.assertEqual(summarize(report([row]))["model_matrix_execution_gate"], "FAIL")

    def test_manifest_identity_failure_fails_closed(self):
        result = summarize(report(), identity_failures=["manifest_mismatch:model_lock_sha256"])
        self.assertEqual(result["identity_gate"], "FAIL")
        self.assertEqual(result["model_matrix_execution_gate"], "FAIL")
        self.assertIn("manifest_mismatch:model_lock_sha256", result["identity_failures"])

    def test_complete_model_matrix_passes_execution_only(self):
        result = summarize(report(), identity_failures=[])
        self.assertEqual(result["model_matrix_execution_gate"], "PASS")
        self.assertEqual(result["identity_gate"], "PASS")
        self.assertEqual(result["recognition_quality_gate"], "NOT_RUN")


if __name__ == "__main__":
    unittest.main()
