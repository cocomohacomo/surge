#!/usr/bin/env python3
"""Strict, fail-closed evaluator for independently adjudicated SURGE corpora."""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path


def norm(value):
    if value is None:
        return ""
    return " ".join(str(value).casefold().strip().split())


def counts(tp, fp, fn):
    # Undefined denominators are deliberately zero, never perfect scores.
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn) if tp + fn else 0.0
    return {"tp": tp, "fp": fp, "fn": fn, "precision": precision,
            "recall": recall,
            "f1": 2 * precision * recall / (precision + recall)
            if precision + recall else 0.0}


def _attribute(item):
    if not isinstance(item, dict):
        return None
    key = norm(item.get("key", item.get("attribute", "")))
    value = norm(item.get("value", item.get("text", "")))
    if not key or not value:
        return None
    return key, value, norm(item.get("polarity", "positive"))


def evaluate_case(case, pred):
    if not isinstance(case, dict) or not isinstance(pred, dict):
        raise ValueError("case and prediction must be objects")
    case_id = case.get("case_id")
    if not isinstance(case_id, str) or not case_id:
        raise ValueError("case_id must be a non-empty string")
    gold = case.get("gold")
    if not isinstance(gold, dict):
        raise ValueError(f"{case_id}: gold must be an object")
    context = case.get("context", "")
    source_text = case.get("text", "")
    if not isinstance(context, str) or not isinstance(source_text, str):
        raise ValueError(f"{case_id}: context/text must be strings")
    source = norm(context + " " + source_text)

    gold_attrs = gold.get("attributes", [])
    gold_rels = gold.get("relations", [])
    if not isinstance(gold_attrs, list) or not isinstance(gold_rels, list):
        raise ValueError(f"{case_id}: gold attributes/relations must be arrays")
    ga = {_attribute(item) for item in gold_attrs}
    ga.discard(None)

    schema_errors = 0
    raw_attrs = pred.get("attributes", pred.get("candidates"))
    if not isinstance(raw_attrs, list):
        raw_attrs = []
        schema_errors += 1
    parsed_attrs, unsupported = [], 0
    evidence_supported = 0
    for item in raw_attrs:
        attr = _attribute(item)
        if attr is None:
            schema_errors += 1
            continue
        parsed_attrs.append((attr, item))
        quote = norm(item.get("evidence_quote", item.get("evidence", "")))
        if quote and quote in source:
            evidence_supported += 1
        else:
            unsupported += 1

    predicted_attrs = [item[0] for item in parsed_attrs]
    unique_attrs = set(predicted_attrs)
    duplicate_attrs = len(predicted_attrs) - len(unique_attrs)
    attr_metrics = counts(len(ga & unique_attrs),
                          len(unique_attrs - ga) + duplicate_attrs + schema_errors,
                          len(ga - unique_attrs))

    gr = set()
    for item in gold_rels:
        if not isinstance(item, dict):
            raise ValueError(f"{case_id}: malformed gold relation")
        values = tuple(norm(item.get(key, "")) for key in ("type", "source", "target"))
        if not all(values):
            raise ValueError(f"{case_id}: incomplete gold relation")
        gr.add(values)
    raw_rels = pred.get("relations")
    if not isinstance(raw_rels, list):
        raw_rels = []
        schema_errors += 1
    parsed_rels = []
    for item in raw_rels:
        if not isinstance(item, dict) or not all(
                isinstance(item.get(key), str) and item[key].strip()
                for key in ("type", "source", "target")):
            schema_errors += 1
            continue
        parsed_rels.append(tuple(norm(item[key]) for key in ("type", "source", "target")))
    unique_rels = set(parsed_rels)
    duplicate_rels = len(parsed_rels) - len(unique_rels)
    rel_metrics = counts(len(gr & unique_rels),
                         len(unique_rels - gr) + duplicate_rels,
                         len(gr - unique_rels))

    # Missing category is an explicit prediction error, not an invisible label.
    missing_category = "__missing_category__"
    gold_categories = {norm(item.get("category", "")) for item in gold_attrs
                       if isinstance(item, dict) and norm(item.get("category", ""))}
    declared = case.get("categories", [])
    if not isinstance(declared, list):
        raise ValueError(f"{case_id}: categories must be an array")
    declared_categories = {norm(value) for value in declared if norm(value)}
    predicted_categories = {
        norm(item.get("category", "")) or missing_category
        for _, item in parsed_attrs
    }
    categories = {}
    for category in sorted(gold_categories | declared_categories | predicted_categories):
        category_gold = {_attribute(item) for item in gold_attrs
                         if isinstance(item, dict) and norm(item.get("category", "")) == category}
        category_gold.discard(None)
        category_pred = {_attribute(item) for _, item in parsed_attrs
                         if (norm(item.get("category", "")) or missing_category) == category}
        category_pred.discard(None)
        category_duplicates = sum(
            1 for _, item in parsed_attrs
            if (norm(item.get("category", "")) or missing_category) == category
        ) - len(category_pred)
        categories[category] = counts(
            len(category_gold & category_pred),
            len(category_pred - category_gold) + category_duplicates,
            len(category_gold - category_pred))

    evidence_total = len(parsed_attrs)
    return {
        "case_id": case_id,
        "attribute_metrics": attr_metrics, "relation_metrics": rel_metrics,
        "attribute": attr_metrics, "relation": rel_metrics,
        "categories": categories,
        "duplicate_attribute_count": duplicate_attrs,
        "duplicate_relation_count": duplicate_rels,
        "malformed_count": schema_errors,
        "schema_valid": schema_errors == 0,
        "unsupported_attribute_count": unsupported,
        "evidence_total": evidence_total,
        "evidence_supported": evidence_supported,
        "evidence_support_rate": evidence_supported / evidence_total if evidence_total else 0.0,
        "false_positive_relation_rate": rel_metrics["fp"] / len(parsed_rels)
        if parsed_rels else (0.0 if not gr else 1.0),
    }


def evaluate(corpus, predictions, corpus_sha, profile_sha=None, commit_sha=None):
    if not isinstance(corpus, dict) or not isinstance(predictions, dict):
        raise ValueError("corpus and predictions must be JSON objects")
    cases = corpus.get("cases", [])
    if not isinstance(cases, list):
        raise ValueError("corpus.cases must be an array")
    ids = [case.get("case_id") for case in cases if isinstance(case, dict)]
    if len(ids) != len(cases) or any(not isinstance(value, str) or not value for value in ids):
        raise ValueError("every corpus case requires a non-empty case_id")
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate case_id in corpus")
    splits = {}
    for case in cases:
        splits.setdefault(case.get("split"), set()).add(case["case_id"])
    if any(splits.get(a, set()) & splits.get(b, set())
           for a, b in (("train", "test"), ("dev", "test"), ("development", "test"))):
        raise ValueError("train/development and test case IDs must be disjoint")

    test_cases = [case for case in cases if case.get("split") == "test"]
    annotation = corpus.get("annotation", {})
    identity = {"corpus_sha256": corpus_sha, "profile_sha256": profile_sha,
                "commit_sha": commit_sha}
    if not test_cases or not isinstance(annotation, dict) or annotation.get("independent_adjudication") is not True:
        return {"recognition_quality_gate": "NOT_RUN",
                "reason": "no independently adjudicated held-out test corpus",
                "case_count": len(test_cases), **identity}

    expected_ids = {case["case_id"] for case in test_cases}
    extra_ids = sorted(set(predictions) - expected_ids)
    missing_ids = sorted(expected_ids - set(predictions))
    if extra_ids or missing_ids:
        return {"recognition_quality_gate": "FAIL",
                "reason": "prediction case coverage mismatch",
                "missing_case_ids": missing_ids, "extra_case_ids": extra_ids,
                "case_count": len(test_cases), **identity}

    results = [evaluate_case(case, predictions[case["case_id"]]) for case in test_cases]

    def aggregate(field):
        return counts(sum(row[field]["tp"] for row in results),
                      sum(row[field]["fp"] for row in results),
                      sum(row[field]["fn"] for row in results))

    attr_metrics, rel_metrics = aggregate("attribute_metrics"), aggregate("relation_metrics")
    category_names = sorted({name for row in results for name in row["categories"]})
    category_metrics = {}
    for category in category_names:
        values = [row["categories"][category] for row in results if category in row["categories"]]
        category_metrics[category] = counts(
            sum(value["tp"] for value in values), sum(value["fp"] for value in values),
            sum(value["fn"] for value in values))
    supported_categories = {
        category: sum(value["tp"] + value["fn"] for row in results
                      for name, value in row["categories"].items() if name == category)
        for category in category_names
    }
    scored = [metric["f1"] for name, metric in category_metrics.items()
              if supported_categories[name] > 0]
    macro_f1 = sum(scored) / len(scored) if scored else 0.0
    evidence_total = sum(row["evidence_total"] for row in results)
    evidence_supported = sum(row["evidence_supported"] for row in results)
    evidence_rate = evidence_supported / evidence_total if evidence_total else 0.0
    predicted_relations = sum(row["relation_metrics"]["tp"] + row["relation_metrics"]["fp"]
                              for row in results)
    relation_fp = sum(row["relation_metrics"]["fp"] for row in results)
    fp_relation_rate = relation_fp / predicted_relations if predicted_relations else 0.0
    schema_valid_count = sum(row["schema_valid"] for row in results)
    schema_valid_rate = schema_valid_count / len(results)
    profile = corpus.get("profile", {})
    if not isinstance(profile, dict):
        raise ValueError("corpus.profile must be an object")
    failures = []
    required = ("attribute_f1_min", "relation_f1_min", "macro_f1_min",
                "evidence_support_min", "schema_valid_rate_min")
    for key in required:
        if key not in profile:
            failures.append("profile_missing:" + key)
    for key, metric in (("attribute_f1_min", attr_metrics),
                        ("relation_f1_min", rel_metrics)):
        if key in profile and metric["f1"] < profile[key]:
            failures.append(key)
    if "macro_f1_min" in profile and macro_f1 < profile["macro_f1_min"]:
        failures.append("macro_f1_min")
    if "evidence_support_min" in profile and evidence_rate < profile["evidence_support_min"]:
        failures.append("evidence_support_min")
    if "schema_valid_rate_min" in profile and schema_valid_rate < profile["schema_valid_rate_min"]:
        failures.append("schema_valid_rate_min")
    if "false_positive_relation_rate_max" not in profile:
        failures.append("profile_missing:false_positive_relation_rate_max")
    elif fp_relation_rate > profile["false_positive_relation_rate_max"]:
        failures.append("false_positive_relation_rate_max")
    floors = profile.get("category_recall_min", {})
    if not isinstance(floors, dict) or not floors:
        failures.append("profile_missing:category_recall_min")
        floors = {}
    for category, floor in floors.items():
        metric = category_metrics.get(category)
        if metric is None or supported_categories.get(category, 0) == 0:
            failures.append("category_without_gold_support:" + category)
        elif metric["recall"] < floor:
            failures.append("category_recall:" + category)

    return {
        "recognition_quality_gate": "PASS" if not failures else "FAIL",
        "failures": failures, "attribute_metrics": attr_metrics,
        "relation_metrics": rel_metrics, "category_metrics": category_metrics,
        "category_gold_support": supported_categories,
        "macro_f1": macro_f1, "schema_valid_rate": schema_valid_rate,
        "evidence_support_rate": evidence_rate,
        "false_positive_relation_rate": fp_relation_rate,
        "unsupported_attribute_count": sum(row["unsupported_attribute_count"] for row in results),
        "duplicate_attribute_count": sum(row["duplicate_attribute_count"] for row in results),
        "duplicate_relation_count": sum(row["duplicate_relation_count"] for row in results),
        "malformed_count": sum(row["malformed_count"] for row in results),
        "case_count": len(results), "per_case": results, **identity,
    }


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: evaluate_gold_corpus.py CORPUS.json PREDICTIONS.json OUTPUT.json")
    corpus_path = Path(sys.argv[1])
    corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
    predictions = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
    profile = json.dumps(corpus.get("profile", {}), sort_keys=True,
                         separators=(",", ":")).encode()
    result = evaluate(corpus, predictions,
                      hashlib.sha256(corpus_path.read_bytes()).hexdigest(),
                      hashlib.sha256(profile).hexdigest())
    Path(sys.argv[3]).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["recognition_quality_gate"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
