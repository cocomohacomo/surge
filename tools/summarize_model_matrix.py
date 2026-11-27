#!/usr/bin/env python3
"""Strict model-matrix execution audit; this never certifies semantic quality."""
import hashlib
import json
import math
import os
import sys
from pathlib import Path


def _valid_metric(metric):
    if not isinstance(metric, dict):
        return False
    if not all(isinstance(metric.get(key), int) and not isinstance(metric.get(key), bool)
               and metric[key] >= 0 for key in ("tp", "fp", "fn")):
        return False
    return all(isinstance(metric.get(key), (int, float))
               and not isinstance(metric.get(key), bool)
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


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _identity_audit(report, lock_path, schema_path, corpus_path, expected_commit):
    manifest = report.get("experiment_manifest")
    failures = []
    if not isinstance(manifest, dict):
        return ["manifest_missing"]
    lock = json.loads(Path(lock_path).read_text(encoding="utf-8"))
    enabled = {item["name"]: item for item in lock.get("models", [])
               if isinstance(item, dict) and item.get("enabled", True)}
    actual_models = report.get("models", [])
    actual_names = {item.get("name") for item in actual_models if isinstance(item, dict)}
    if actual_names != set(enabled):
        failures.append("model_lock_names_mismatch")
    for item in actual_models:
        if not isinstance(item, dict):
            failures.append("model_entry_invalid")
            continue
        locked = enabled.get(item.get("name"))
        if locked is None:
            continue
        for actual_key, lock_key in (("repository", "repo"),
                                     ("repository_revision", "revision"),
                                     ("tokenizer_repository", "tokenizer_repo"),
                                     ("tokenizer_revision", "tokenizer_revision"),
                                     ("model_file", "file")):
            if item.get(actual_key) != locked.get(lock_key):
                failures.append("model_lock_mismatch:" + str(item.get("name")) + ":" + actual_key)
    corpus = json.loads(Path(corpus_path).read_text(encoding="utf-8"))
    profile_digest = hashlib.sha256(json.dumps(corpus.get("profile", {}), sort_keys=True,
        separators=(",", ":")).encode()).hexdigest()
    expected = {
        "llama_cpp_commit": lock.get("llama_cpp", {}).get("commit"),
        "model_lock_sha256": _sha256(lock_path),
        "canonical_schema_sha256": _sha256(schema_path),
        "corpus_sha256": _sha256(corpus_path),
        "profile_sha256": profile_digest,
        "source_commit": expected_commit,
    }
    for key, value in expected.items():
        if not value or manifest.get(key) != value:
            failures.append("manifest_mismatch:" + key)
    if manifest.get("manifest_version") != "surge-experiment-manifest-v1.4":
        failures.append("manifest_version_mismatch")
    if manifest.get("execution_backend") != "llama-completion":
        failures.append("manifest_execution_backend_mismatch")
    if manifest.get("prompt_mode") != "preformatted-chatml-noninteractive":
        failures.append("manifest_prompt_mode_mismatch")
    if manifest.get("prompt_transport") != "utf8-temporary-file":
        failures.append("manifest_prompt_transport_mismatch")
    if not isinstance(manifest.get("llama_completion_path"), str) or not manifest["llama_completion_path"]:
        failures.append("manifest_completion_path_missing")
    runtime = manifest.get("runtime")
    if not isinstance(runtime, dict) or not isinstance(runtime.get("llama_completion_version"), str) or not runtime["llama_completion_version"]:
        failures.append("manifest_completion_version_missing")
    return failures


def summarize(report, identity_failures=None):
    """Summarize execution metrics and fail the execution gate on identity audit failures.

    Identity audit is performed by the caller; omitted audit is explicitly NOT_RUN.
    """
    identity_audit_executed = identity_failures is not None
    identity_failures = list(identity_failures or [])
    identity_gate = "PASS" if identity_audit_executed and not identity_failures else ("FAIL" if identity_failures else "NOT_RUN")
    if not isinstance(report, dict):
        raise ValueError("model matrix report must be an object")
    models = report.get("models")
    expected_count = report.get("expected_model_count")
    expected_case_ids = report.get("expected_case_ids")
    if not isinstance(models, list) or not isinstance(expected_count, int) or isinstance(expected_count, bool) or expected_count < 1:
        models = models if isinstance(models, list) else []
        expected_count = expected_count
    case_contract_valid = (isinstance(expected_case_ids, list)
                           and bool(expected_case_ids)
                           and all(isinstance(x, str) and x for x in expected_case_ids)
                           and len(expected_case_ids) == len(set(expected_case_ids)))
    expected_cases = set(expected_case_ids) if case_contract_valid else set()
    names = [model.get("name") for model in models if isinstance(model, dict)]
    model_set_valid = (
        isinstance(expected_count, int) and not isinstance(expected_count, bool) and expected_count > 0
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
        execution_errors = sum(bool(row.get("execution_error")) for row in rows
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
        counter_fields = ("unsupported_attribute_count", "duplicate_attribute_count",
                          "duplicate_relation_count", "duplicate_evidence_span_count", "malformed_count")
        counters_valid = all(
            isinstance(item.get(key), int) and not isinstance(item.get(key), bool)
            and item[key] >= 0
            for item in semantic for key in counter_fields
        )
        evidence_valid_cases = sum(row.get("evidence_spans_valid") is True for row in rows)
        evidence_invalid_cases = len(rows) - evidence_valid_cases
        evidence_gate = "PASS" if rows and evidence_invalid_cases == 0 else "FAIL"
        complete_metrics = bool(rows) and len(semantic) == len(rows) and metric_shape_valid and counters_valid
        execution_pass = (
            bool(rows) and case_coverage_valid and not invalid_json and not invalid_schema
            and not execution_errors and not timeouts and not missing_rss
            and not nonzero_exit and complete_metrics and evidence_gate == "PASS"
        )
        model_reports.append({
            "name": model.get("name"), "cases": len(rows),
            "case_coverage_valid": case_coverage_valid,
            "coverage": len(semantic) / len(rows) if rows else 0.0,
            "invalid_json": invalid_json, "invalid_schema": invalid_schema,
            "execution_errors": execution_errors, "timeouts": timeouts,
            "missing_rss": missing_rss, "nonzero_exit": nonzero_exit,
            "metrics_complete": complete_metrics, "counters_valid": counters_valid,
            "evidence_valid_cases": evidence_valid_cases, "evidence_invalid_cases": evidence_invalid_cases,
            "evidence_gate": evidence_gate,
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
    ) and identity_gate == "PASS"
    evidence_pass = model_set_valid and all(model["evidence_gate"] == "PASS" for model in model_reports)"
    corpus_status = report.get("corpus_status")
    return {
        "model_matrix_execution_gate": "PASS" if execution_pass else "FAIL",
        "model_matrix_evidence_gate": "PASS" if evidence_pass else "FAIL",
        "identity_gate": identity_gate,
        "identity_failures": identity_failures,
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
    report = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    root = Path(__file__).resolve().parents[1]
    lock_path = Path(os.environ.get("SURGE_MODEL_MATRIX_LOCK", root / "config/model-matrix-lock-v1.0.json"))
    schema_path = root / "schemas/surge-projector-output-v1.0.schema.json"
    corpus_path = Path(os.environ.get("SURGE_GOLD_CORPUS", root / "tests/fixtures/semantic/gold_seed_v0.1.json"))
    expected_commit = os.environ.get("GITHUB_SHA")
    identity_failures = _identity_audit(report, lock_path, schema_path, corpus_path, expected_commit)
    if not expected_commit:
        identity_failures.append("source_commit_unavailable")
    result = summarize(report, identity_failures=identity_failures)
    Path(sys.argv[2]).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["model_matrix_execution_gate"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
