#!/usr/bin/env python3
"""Strict structured semantic evaluator. Synthetic/unadjudicated corpora never qualify."""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path


def norm(value):
    return " ".join(str(value).casefold().strip().split())


def counts(tp, fp, fn):
    precision = tp / (tp + fp) if tp + fp else (1.0 if fn == 0 else 0.0)
    recall = tp / (tp + fn) if tp + fn else 1.0
    return {"tp": tp, "fp": fp, "fn": fn, "precision": precision,
            "recall": recall, "f1": 2 * precision * recall / (precision + recall)
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
    source = norm(case.get("context", "") + " " + case.get("text", ""))
    gold = case.get("gold", {})
    gold_attrs = gold.get("attributes", [])
    gold_rels = gold.get("relations", [])
    ga = {_attribute(item) for item in gold_attrs}
    ga.discard(None)

    raw_attrs = pred.get("attributes", pred.get("candidates", []))
    malformed = 0
    if not isinstance(raw_attrs, list):
        raw_attrs = []
        malformed += 1
    parsed_attrs, unsupported = [], 0
    evidence_supported = 0
    for item in raw_attrs:
        attr = _attribute(item)
        if attr is None:
            malformed += 1
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
                          len(unique_attrs - ga) + duplicate_attrs + malformed,
                          len(ga - unique_attrs))

    gr = {(norm(item.get("type", "")), norm(item.get("source", "")),
           norm(item.get("target", ""))) for item in gold_rels
          if isinstance(item, dict)}
    raw_rels = pred.get("relations", [])
    rel_malformed = 0
    if not isinstance(raw_rels, list):
        raw_rels = []
        rel_malformed += 1
    parsed_rels = []
    for item in raw_rels:
        if not isinstance(item, dict) or not all(
                isinstance(item.get(key), str) and item[key].strip()
                for key in ("type", "source", "target")):
            rel_malformed += 1
            continue
        parsed_rels.append((norm(item["type"]), norm(item["source"]), norm(item["target"])))
    unique_rels = set(parsed_rels)
    duplicate_rels = len(parsed_rels) - len(unique_rels)
    rel_metrics = counts(len(gr & unique_rels),
                         len(unique_rels - gr) + duplicate_rels + rel_malformed,
                         len(gr - unique_rels))

    # A wrong/missing category must not disappear from category-level scoring:
    # it is an FN in the gold category and an FP in the predicted category.
    gold_categories = {norm(item.get("category", "")) for item in gold_attrs
                       if isinstance(item, dict) and norm(item.get("category", ""))}
    declared_categories = {norm(value) for value in case.get("categories", [])}
    predicted_categories = {norm(item.get("category", "")) for _, item in parsed_attrs
                            if norm(item.get("category", ""))}
    categories = {}
    for category in sorted(gold_categories | declared_categories | predicted_categories):
        category_gold = {_attribute(item) for item in gold_attrs
                         if isinstance(item, dict) and norm(item.get("category", "")) == category}
        category_gold.discard(None)
        category_pred = {_attribute(item) for _, item in parsed_attrs
                         if norm(item.get("category", "")) == category}
        category_pred.discard(None)
        category_duplicates = sum(
            1 for attr, item in parsed_attrs
            if norm(item.get("category", "")) == category
        ) - len(category_pred)
        categories[category] = counts(
            len(category_gold & category_pred),
            len(category_pred - category_gold) + category_duplicates,
            len(category_gold - category_pred),
        )

    evidence_total = len(parsed_attrs)
    return {
        "case_id": case["case_id"],
        "attribute": attr_metrics, "relation": rel_metrics,
        "attribute_metrics": attr_metrics, "relation_metrics": rel_metrics,
        "categories": categories,
        "duplicate_attribute_count": duplicate_attrs,
        "duplicate_relation_count": duplicate_rels,
        "malformed_count": malformed + rel_malformed,
        "unsupported_attribute_count": unsupported,
        "evidence_total": evidence_total,
        "evidence_supported": evidence_supported,
        "evidence_support_rate": evidence_supported / evidence_total if evidence_total else 0.0,
    }


def evaluate(corpus, predictions, corpus_sha, profile_sha=None, commit_sha=None):
    test_cases = [case for case in corpus.get("cases", []) if case.get("split") == "test"]
    annotation = corpus.get("annotation", {})
    identity = {"corpus_sha256": corpus_sha, "profile_sha256": profile_sha,
                "commit_sha": commit_sha}
    if not test_cases or annotation.get("independent_adjudication") is not True:
        return {"recognition_quality_gate": "NOT_RUN",
                "reason": "no independently adjudicated held-out test corpus",
                "case_count": len(test_cases), **identity}

    results = []
    for case in test_cases:
        prediction = predictions.get(case["case_id"])
        if not isinstance(prediction, dict):
            return {"recognition_quality_gate": "FAIL",
                    "reason": "missing or invalid prediction: " + case["case_id"],
                    "case_count": len(test_cases), **identity}
        results.append(evaluate_case(case, prediction))

    def aggregate(field):
        return counts(sum(row[field]["tp"] for row in results),
                      sum(row[field]["fp"] for row in results),
                      sum(row[field]["fn"] for row in results))

    attr_metrics = aggregate("attribute")
    rel_metrics = aggregate("relation")
    category_names = sorted({name for row in results for name in row["categories"]})
    category_metrics = {}
    for category in category_names:
        values = [row["categories"][category] for row in results if category in row["categories"]]
        category_metrics[category] = counts(
            sum(value["tp"] for value in values), sum(value["fp"] for value in values),
            sum(value["fn"] for value in values))

    evidence_total = sum(row["evidence_total"] for row in results)
    evidence_supported = sum(row["evidence_supported"] for row in results)
    evidence_rate = evidence_supported / evidence_total if evidence_total else 0.0
    profile = corpus.get("profile", {})
    failures = []
    for key, metric in (("attribute_f1_min", attr_metrics), ("relation_f1_min", rel_metrics)):
        if metric["f1"] < profile.get(key, 1.1):
            failures.append(key)
    if evidence_rate < profile.get("evidence_support_min", 1.1):
        failures.append("evidence_support_min")
    floors = profile.get("category_recall_min", {})
    for category, floor in floors.items():
        if category not in category_metrics or category_metrics[category]["recall"] < floor:
            failures.append("category_recall:" + category)

    return {
        "recognition_quality_gate": "PASS" if not failures else "FAIL",
        "failures": failures, "attribute_metrics": attr_metrics,
        "relation_metrics": rel_metrics, "category_metrics": category_metrics,
        "evidence_support_rate": evidence_rate,
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
    result = evaluate(corpus, predictions, hashlib.sha256(corpus_path.read_bytes()).hexdigest(),
                      hashlib.sha256(profile).hexdigest())
    Path(sys.argv[3]).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["recognition_quality_gate"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
