#!/usr/bin/env python3
"""Strictly summarize model-matrix outputs; execution success is not qualification."""
import json
import sys
from pathlib import Path


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
    model_reports = []
    for model in report.get("models", []):
        rows = model.get("results", [])
        invalid_json = sum(not row.get("json_valid", False) for row in rows)
        timeouts = sum(bool(row.get("timeout")) for row in rows)
        missing_rss = sum(row.get("peak_rss_kib") is None for row in rows
                          if not row.get("timeout"))
        semantic = [row.get("engineering_semantic_metrics") for row in rows
                    if isinstance(row.get("engineering_semantic_metrics"), dict)]
        metric_shape_valid = all(
            isinstance(item.get(key), dict)
            and all(name in item[key] for name in ("tp", "fp", "fn", "precision", "recall", "f1"))
            for item in semantic for key in ("attribute_metrics", "relation_metrics")
        )
        complete_metrics = bool(rows) and len(semantic) == len(rows) and metric_shape_valid
        execution_pass = bool(rows) and not invalid_json and not timeouts and not missing_rss and complete_metrics
        model_reports.append({
            "name": model.get("name"), "cases": len(rows),
            "coverage": len(semantic) / len(rows) if rows else 0.0,
            "invalid_json": invalid_json, "timeouts": timeouts,
            "missing_rss": missing_rss, "metrics_complete": complete_metrics,
            "attribute": _aggregate(semantic, "attribute_metrics") if semantic and metric_shape_valid else None,
            "relation": _aggregate(semantic, "relation_metrics") if semantic and metric_shape_valid else None,
            "unsupported_attributes": sum(item.get("unsupported_attribute_count", 0) for item in semantic),
            "duplicate_attributes": sum(item.get("duplicate_attribute_count", 0) for item in semantic),
            "duplicate_relations": sum(item.get("duplicate_relation_count", 0) for item in semantic),
            "malformed": sum(item.get("malformed_count", 0) for item in semantic),
            "execution_gate": "PASS" if execution_pass else "FAIL",
        })

    expected_count = report.get("expected_model_count")
    model_set_valid = bool(model_reports) and (
        expected_count is None or len(model_reports) == expected_count
    )
    execution_pass = model_set_valid and all(
        model["execution_gate"] == "PASS" for model in model_reports
    )
    return {
        "model_matrix_execution_gate": "PASS" if execution_pass else "FAIL",
        "recognition_quality_gate": "NOT_RUN"
        if report.get("corpus_status") == "SEED_NOT_INDEPENDENT" else "PENDING",
        "model_count": len(model_reports), "expected_model_count": expected_count,
        "model_set_valid": model_set_valid,
        "models": model_reports, "corpus_status": report.get("corpus_status"),
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
