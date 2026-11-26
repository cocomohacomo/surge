#!/usr/bin/env python3
"""Strict model-matrix execution audit; this never certifies semantic quality."""
import json
import math
import sys
from pathlib import Path


def _valid_metric(metric):
    if not isinstance(metric, dict):
        return False
    if not all(isinstance(metric.get(key), int) and metric[key] >= 0
               for key in ("tp", "fp", "fn")):
        return False
    return all(isinstance(metric.get(key), (int, float))
               and math.isfinite(metric[key]) and 0.0 <= metric[key] <= 1.0
               for key in ("precision", "recall", "f1"))


def _aggregate(metrics, key):
    values = [item[key] for item in metrics]
    tp = sum(value["tp"] for value in values)
    fp = sum(value["fp"] for value in values)
    fn = sum(value["fn"] for value in values)
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn) if tp + fn else 0.0
    return {"tp": tp, "fp": fp, "fn": fn, "precision": precision,
            "recall": recall,
            "f1": 2 * precision * recall / (precision + recall)
            if precision + recall else 0.0}


def summarize(report):
    if not isinstance(report, dict):
        raise ValueError("model matrix report must be an object")
    models = report.get("models")
    expected_count = report.get("expected_model_count")
    expected_case_ids = report.get("expected_case_ids")
    if not isinstance(models, list) or not isinstance(expected_count, int) or expected_count < 1:
        models = models if isinstance(models, list) else []
        expected_count = expected_count
    case_contract_valid = (isinstance(expected_case_ids, list)
                           and bool(expected_case_ids)
                           and all(isinstance(x, str) and x for x in expected_case_ids)
                           and len(expected_case_ids) == len(set(expected_case_ids)))
    expected_cases = set(expected_case_ids) if case_contract_valid else set()
    names = [model.get("name") for model in models if isinstance(model, dict)]
    model_set_valid = (
        isinstance(expected_count, int) and expected_count > 0
        and len(models) == expected_count
        and len(names) == len(models)
        and all(isinstance(name, str) and name for name in names)
        and len(names) == len(set(names))
        and case_contract_valid
    )

    model_reports = []
    for model in models:
        if not isinstance(model, dict):
            model = {}
        rows = model.get("results", [])
        if not isinstance(rows, list):
            rows = []
        case_ids = [row.get("case_id") for row in rows if isinstance(row, dict)]
        case_coverage_valid = (
            len(case_ids) == len(rows)
            and all(isinstance(x, str) and x for x in case_ids)
            and len(case_ids) == len(set(case_ids))
            and set(case_ids) == expected_cases
        ) if case_contract_valid else False
        invalid_json = sum(not row.get("json_valid", False) for row in rows
                           if isinstance(row, dict))
        invalid_schema = sum(not row.get("projector_schema_valid", False) for row in rows
                             if isinstance(row, dict))
        timeouts = sum(bool(row.get("timeout")) for row in rows if isinstance(row, dict))
        missing_rss = sum(
            row.get("peak_rss_kib") is None for row in rows
            if isinstance(row, dict) and not row.get("timeout")
        )
        nonzero_exit = sum(
            row.get("return_code") != 0 for row in rows
            if isinstance(row, dict) and not row.get("timeout")
        )
        semantic = [row.get("engineering_semantic_metrics") for row in rows
                    if isinstance(row, dict)
                    and isinstance(row.get("engineering_semantic_metrics"), dict)]
        metric_shape_valid = all(
            _valid_metric(item.get(key))
            for item in semantic for key in ("attribute_metrics", "relation_metrics")
        ) and all(
            _valid_metric(item.get("evidence_span_metrics"))
            and _valid_metric(item.get("attribute_evidence_span_metrics"))
            and _valid_metric(item.get("relation_evidence_span_metrics"))
            and isinstance(item.get("categories"), dict) and bool(item["categories"])
            and all(_valid_metric(metric) for metric in item["categories"].values())
            and isinstance(item.get("phenomenon_metrics"), dict)
            and set(item["phenomenon_metrics"]) == {"negation", "contradiction", "temporal_supersession", "ambiguity"}
            and all(_valid_metric(metric) for metric in item["phenomenon_metrics"].values())
            for item in semantic
        )
        complete_metrics = bool(rows) and len(semantic) == len(rows) and metric_shape_valid
        execution_pass = (
            bool(rows) and case_coverage_valid and not invalid_json and not invalid_schema
            and not timeouts and not missing_rss and not nonzero_exit and complete_metrics
        )
        model_reports.append({
            "name": model.get("name"), "cases": len(rows),
            "case_coverage_valid": case_coverage_valid,
            "coverage": len(semantic) / len(rows) if rows else 0.0,
            "invalid_json": invalid_json, "invalid_schema": invalid_schema,
            "timeouts": timeouts,
            "missing_rss": missing_rss, "nonzero_exit": nonzero_exit,
            "metrics_complete": complete_metrics,
            "attribute": _aggregate(semantic, "attribute_metrics") if semantic and metric_shape_valid else None,
            "relation": _aggregate(semantic, "relation_metrics") if semantic and metric_shape_valid else None,
            "categories": {
                category: _aggregate([{"metric": item["categories"][category]} for item in semantic
                                       if category in item["categories"]], "metric")
                for category in sorted({name for item in semantic for name in item.get("categories", {})})
            } if semantic and metric_shape_valid else None,
            "phenomena": {
                phenomenon: _aggregate([{"metric": item["phenomenon_metrics"][phenomenon]}
                                        for item in semantic if phenomenon in item["phenomenon_metrics"]], "metric")
                for phenomenon in sorted({name for item in semantic for name in item.get("phenomenon_metrics", {})})
            } if semantic and metric_shape_valid else None,
            "evidence_spans": _aggregate(semantic, "evidence_span_metrics")
            if semantic and metric_shape_valid and all("evidence_span_metrics" in item for item in semantic) else None,
            "unsupported_attributes": sum(item.get("unsupported_attribute_count", 0) for item in semantic),
            "duplicate_attributes": sum(item.get("duplicate_attribute_count", 0) for item in semantic),
            "duplicate_relations": sum(item.get("duplicate_relation_count", 0) for item in semantic),
            "duplicate_evidence_spans": sum(item.get("duplicate_evidence_span_count", 0) for item in semantic),
            "malformed": sum(item.get("malformed_count", 0) for item in semantic),
            "execution_gate": "PASS" if execution_pass else "FAIL",
        })

    execution_pass = model_set_valid and all(
        model["execution_gate"] == "PASS" for model in model_reports
    )
    corpus_status = report.get("corpus_status")
    return {
        "model_matrix_execution_gate": "PASS" if execution_pass else "FAIL",
        "recognition_quality_gate": "NOT_RUN"
        if corpus_status == "SEED_NOT_INDEPENDENT" else "PENDING",
        "model_count": len(model_reports), "expected_model_count": expected_count,
        "model_set_valid": model_set_valid,
        "expected_case_count": len(expected_cases),
        "corpus_status": corpus_status, "models": model_reports,
    }


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: summarize_model_matrix.py INPUT.json OUTPUT.json")
    result = summarize(json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")))
    Path(sys.argv[2]).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["model_matrix_execution_gate"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
