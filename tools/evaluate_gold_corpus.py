#!/usr/bin/env python3
"""Strict, fail-closed evaluator for independently adjudicated SURGE corpora."""
from __future__ import annotations

import hashlib
import json
import math
import sys
from jsonschema import Draft202012Validator
from pathlib import Path


_SCHEMA_PATH = Path(__file__).resolve().parents[1] / "schemas" / "surge-projector-output-v1.0.schema.json"
_PROJECTOR_SCHEMA = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
_PROJECTOR_VALIDATOR = Draft202012Validator(_PROJECTOR_SCHEMA)


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
    source_parts = {"context": context, "text": source_text}
    source = norm(context + " " + source_text)

    def span_key(span, parts):
        if not isinstance(span, dict):
            return None
        source_name = span.get("source")
        start, end, quote = span.get("start"), span.get("end"), span.get("quote")
        if not isinstance(source_name, str) or source_name not in parts or not isinstance(start, int) or isinstance(start, bool) or not isinstance(end, int) or isinstance(end, bool):
            return None
        segment = parts[source_name]
        if start < 0 or end <= start or end > len(segment) or not isinstance(quote, str) or not quote:
            return None
        if segment[start:end] != quote:
            return None
        return source_name, start, end, quote

    gold_attrs = gold.get("attributes", [])
    gold_rels = gold.get("relations", [])
    if not isinstance(gold_attrs, list) or not isinstance(gold_rels, list):
        raise ValueError(f"{case_id}: gold attributes/relations must be arrays")
    ga = {_attribute(item) for item in gold_attrs}
    ga.discard(None)
    gold_spans = set()
    gold_attribute_spans = {}
    for item in gold_attrs:
        if isinstance(item, dict):
            spans = item.get("evidence_spans", [])
            if not isinstance(spans, list):
                raise ValueError(f"{case_id}: gold evidence_spans must be an array")
            for span in spans:
                key = span_key(span, source_parts)
                if key is None:
                    raise ValueError(f"{case_id}: invalid gold evidence span")
                gold_spans.add(key)
                identity = _attribute(item)
                if identity is not None:
                    gold_attribute_spans.setdefault(identity, set()).add(key)

    schema_errors = 0
    canonical_schema_errors = list(_PROJECTOR_VALIDATOR.iter_errors(pred))
    raw_attrs = pred.get("attributes")
    if not isinstance(raw_attrs, list):
        raw_attrs = []
        schema_errors += 1
    parsed_attrs, unsupported = [], 0
    evidence_supported = 0
    predicted_spans = []
    for item in raw_attrs:
        attr = _attribute(item)
        if attr is None:
            schema_errors += 1
            continue
        parsed_attrs.append((attr, item))
        spans = item.get("evidence_spans", [])
        if not isinstance(spans, list):
            schema_errors += 1
            spans = []
        valid_spans = []
        for span in spans:
            key = span_key(span, source_parts)
            if key is None:
                schema_errors += 1
                continue
            valid_spans.append(key)
            predicted_spans.append(key)
        quote = norm(item.get("evidence_quote", item.get("evidence", "")))
        # A syntactically valid span is not sufficient: it must be adjudicated
        # as evidence for this exact attribute in the Gold corpus.
        adjudicated_spans = gold_attribute_spans.get(attr, set())
        if adjudicated_spans.intersection(valid_spans):
            evidence_supported += 1
        else:
            unsupported += 1

    predicted_attrs = [item[0] for item in parsed_attrs]
    unique_attrs = set(predicted_attrs)
    duplicate_attrs = len(predicted_attrs) - len(unique_attrs)
    attr_metrics = counts(len(ga & unique_attrs),
                          len(unique_attrs - ga) + duplicate_attrs,
                          len(ga - unique_attrs))

    gr = set()
    gold_relation_spans = set()
    for item in gold_rels:
        if not isinstance(item, dict):
            raise ValueError(f"{case_id}: malformed gold relation")
        values = tuple(norm(item.get(key, "")) for key in ("type", "source", "target"))
        if not all(values):
            raise ValueError(f"{case_id}: incomplete gold relation")
        gr.add(values)
        spans = item.get("evidence_spans", [])
        if not isinstance(spans, list):
            raise ValueError(f"{case_id}: gold relation evidence_spans must be an array")
        for span in spans:
            key = span_key(span, source_parts)
            if key is None:
                raise ValueError(f"{case_id}: invalid gold relation evidence span")
            gold_relation_spans.add(key)
    raw_rels = pred.get("relations")
    if not isinstance(raw_rels, list):
        raw_rels = []
        schema_errors += 1
    parsed_rels, predicted_relation_spans = [], []
    for item in raw_rels:
        if not isinstance(item, dict) or not all(
                isinstance(item.get(key), str) and item[key].strip()
                for key in ("type", "source", "target")):
            schema_errors += 1
            continue
        parsed_rels.append(tuple(norm(item[key]) for key in ("type", "source", "target")))
        spans = item.get("evidence_spans", [])
        if not isinstance(spans, list):
            schema_errors += 1
            continue
        for span in spans:
            key = span_key(span, source_parts)
            if key is None:
                schema_errors += 1
            else:
                predicted_relation_spans.append(key)
    unique_relation_spans = set(predicted_relation_spans)
    duplicate_relation_spans = len(predicted_relation_spans) - len(unique_relation_spans)
    relation_span_metrics = counts(
        len(gold_relation_spans & unique_relation_spans),
        len(unique_relation_spans - gold_relation_spans) + duplicate_relation_spans,
        len(gold_relation_spans - unique_relation_spans))
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

    unique_spans = set(predicted_spans)
    duplicate_spans = len(predicted_spans) - len(unique_spans)
    attribute_span_metrics = counts(len(gold_spans & unique_spans),
                                     len(unique_spans - gold_spans) + duplicate_spans,
                                     len(gold_spans - unique_spans))
    span_metrics = counts(
        attribute_span_metrics["tp"] + relation_span_metrics["tp"],
        attribute_span_metrics["fp"] + relation_span_metrics["fp"],
        attribute_span_metrics["fn"] + relation_span_metrics["fn"])
    # Phenomena are explicit model predictions, never inferred from polarity,
    # attribute category, relation type, or abstention presence.
    predicted_phenomena = set()
    raw_phenomena = pred.get("phenomena", [])
    if not isinstance(raw_phenomena, list):
        schema_errors += 1
        raw_phenomena = []
    valid_phenomena = {"negation", "contradiction", "temporal_supersession", "ambiguity"}
    for item in raw_phenomena:
        if not isinstance(item, dict) or item.get("label") not in valid_phenomena:
            schema_errors += 1
            continue
        spans = item.get("evidence_spans")
        if not isinstance(spans, list) or not spans:
            schema_errors += 1
            continue
        valid_evidence = [span_key(span, source_parts) for span in spans]
        if any(span is None for span in valid_evidence):
            schema_errors += 1
            continue
        predicted_phenomena.add(item["label"])

    evidence_total = len(parsed_attrs)
    return {
        "case_id": case_id,
        "attribute_metrics": attr_metrics, "relation_metrics": rel_metrics,
        "attribute": attr_metrics, "relation": rel_metrics,
        "categories": categories,
        "predicted_phenomena": sorted(predicted_phenomena),
        "gold_phenomena": sorted(case.get("phenomena", []))
        if isinstance(case.get("phenomena", []), list)
        and all(isinstance(label, str) for label in case.get("phenomena", [])) else [],
        "phenomenon_metrics": {
            name: counts(int(name in (case.get("phenomena", []) if isinstance(case.get("phenomena", []), list) else []) and name in predicted_phenomena),
                         int(name not in (case.get("phenomena", []) if isinstance(case.get("phenomena", []), list) else []) and name in predicted_phenomena),
                         int(name in (case.get("phenomena", []) if isinstance(case.get("phenomena", []), list) else []) and name not in predicted_phenomena))
            for name in ("negation", "contradiction", "temporal_supersession", "ambiguity")
        },
        "evidence_span_metrics": span_metrics,
        "attribute_evidence_span_metrics": attribute_span_metrics,
        "relation_evidence_span_metrics": relation_span_metrics,
        "gold_evidence_span_count": len(gold_spans) + len(gold_relation_spans),
        "predicted_evidence_span_count": len(predicted_spans) + len(predicted_relation_spans),
        "duplicate_evidence_span_count": duplicate_spans + duplicate_relation_spans,
        "duplicate_attribute_count": duplicate_attrs,
        "duplicate_relation_count": duplicate_rels,
        "malformed_count": schema_errors,
        "schema_valid": not canonical_schema_errors and schema_errors == 0,
        "canonical_schema_error_count": len(canonical_schema_errors),
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

    if corpus.get("schema_version") != "surge-gold-corpus-v1.0":
        return {"recognition_quality_gate": "FAIL",
                "reason": "unsupported Gold Corpus schema version",
                "expected_schema_version": "surge-gold-corpus-v1.0",
                "actual_schema_version": corpus.get("schema_version"), **identity}

    expected_ids = {case["case_id"] for case in test_cases}
    extra_ids = sorted(set(predictions) - expected_ids)
    missing_ids = sorted(expected_ids - set(predictions))
    if extra_ids or missing_ids:
        return {"recognition_quality_gate": "FAIL",
                "reason": "prediction case coverage mismatch",
                "missing_case_ids": missing_ids, "extra_case_ids": extra_ids,
                "case_count": len(test_cases), **identity}

    valid_phenomena = {"negation", "contradiction", "temporal_supersession", "ambiguity"}
    for case in test_cases:
        labels = case.get("phenomena")
        if not isinstance(labels, list) or any(not isinstance(label, str) or label not in valid_phenomena for label in labels):
            return {"recognition_quality_gate": "FAIL",
                    "reason": "missing or invalid independently annotated phenomena",
                    "case_id": case["case_id"], **identity}
        if len(labels) != len(set(labels)):
            return {"recognition_quality_gate": "FAIL",
                    "reason": "duplicate phenomenon annotation", "case_id": case["case_id"], **identity}
        gold = case.get("gold", {})
        for field in ("attributes", "relations"):
            entries = gold.get(field, []) if isinstance(gold, dict) else []
            if not isinstance(entries, list) or any(
                not isinstance(item, dict) or not isinstance(item.get("evidence_spans"), list)
                or not item["evidence_spans"] for item in entries
            ):
                return {"recognition_quality_gate": "FAIL",
                        "reason": "every held-out Gold attribute and relation requires Evidence Spans",
                        "case_id": case["case_id"], "field": field, **identity}
        if list(_PROJECTOR_VALIDATOR.iter_errors(gold)):
            return {"recognition_quality_gate": "FAIL",
                    "reason": "held-out Gold annotation does not conform to Canonical Projector Schema v1.0",
                    "case_id": case["case_id"], **identity}
    try:
        results = [evaluate_case(case, predictions[case["case_id"]]) for case in test_cases]
    except ValueError as error:
        return {"recognition_quality_gate": "FAIL",
                "reason": "invalid Gold or prediction evidence data: " + str(error), **identity}

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
    span_metrics = counts(
        sum(row["evidence_span_metrics"]["tp"] for row in results),
        sum(row["evidence_span_metrics"]["fp"] for row in results),
        sum(row["evidence_span_metrics"]["fn"] for row in results))
    span_gold_total = sum(row["gold_evidence_span_count"] for row in results)
    relation_span_metrics = counts(
        sum(row["relation_evidence_span_metrics"]["tp"] for row in results),
        sum(row["relation_evidence_span_metrics"]["fp"] for row in results),
        sum(row["relation_evidence_span_metrics"]["fn"] for row in results))
    predicted_relations = sum(row["relation_metrics"]["tp"] + row["relation_metrics"]["fp"]
                              for row in results)
    relation_fp = sum(row["relation_metrics"]["fp"] for row in results)
    fp_relation_rate = relation_fp / predicted_relations if predicted_relations else 0.0
    schema_valid_count = sum(row["schema_valid"] for row in results)
    schema_valid_rate = schema_valid_count / len(results)
    phenomenon_names = ("negation", "contradiction", "temporal_supersession", "ambiguity")
    phenomenon_metrics = {}
    phenomenon_support = {}
    for phenomenon in phenomenon_names:
        tp = fp = fn = 0
        positive_cases = negative_cases = 0
        for case, row in zip(test_cases, results):
            gold_set = set(case.get("phenomena", []))
            pred_set = set(row["predicted_phenomena"])
            positive_cases += int(phenomenon in gold_set)
            negative_cases += int(phenomenon not in gold_set)
            tp += int(phenomenon in gold_set and phenomenon in pred_set)
            fp += int(phenomenon not in gold_set and phenomenon in pred_set)
            fn += int(phenomenon in gold_set and phenomenon not in pred_set)
        phenomenon_metrics[phenomenon] = counts(tp, fp, fn)
        phenomenon_support[phenomenon] = {
            "positive_cases": positive_cases, "negative_cases": negative_cases
        }

    profile = corpus.get("profile", {})
    if not isinstance(profile, dict):
        raise ValueError("corpus.profile must be an object")
    failures = []
    required = ("attribute_f1_min", "relation_f1_min", "macro_f1_min",
                "evidence_support_min", "schema_valid_rate_min",
                "category_precision_min", "category_recall_min",
                "phenomenon_precision_min", "phenomenon_recall_min")
    def valid_threshold(value):
        return (isinstance(value, (int, float)) and not isinstance(value, bool)
                and math.isfinite(value) and 0.0 <= value <= 1.0)
    for key in required:
        if key not in profile:
            failures.append("profile_missing:" + key)
        elif key not in ("category_precision_min", "category_recall_min",
                         "phenomenon_precision_min", "phenomenon_recall_min") and not valid_threshold(profile[key]):
            failures.append("profile_invalid:" + key)
    for key, metric in (("attribute_f1_min", attr_metrics),
                        ("relation_f1_min", rel_metrics),
                        ("macro_f1_min", {"f1": macro_f1}),
                        ("evidence_support_min", {"f1": evidence_rate}),
                        ("schema_valid_rate_min", {"f1": schema_valid_rate})):
        if key in profile and valid_threshold(profile[key]) and metric["f1"] < profile[key]:
            failures.append(key)
    if corpus.get("evidence_span_evaluation") is not True:
        failures.append("evidence_span_evaluation_required")
    if corpus.get("evidence_span_evaluation") is True:
        if "evidence_span_f1_min" not in profile:
            failures.append("profile_missing:evidence_span_f1_min")
        elif not valid_threshold(profile["evidence_span_f1_min"]):
            failures.append("profile_invalid:evidence_span_f1_min")
        elif span_metrics["f1"] < profile["evidence_span_f1_min"]:
            failures.append("evidence_span_f1_min")
        if span_gold_total == 0:
            failures.append("evidence_span_gold_empty")
    if "false_positive_relation_rate_max" not in profile:
        failures.append("profile_missing:false_positive_relation_rate_max")
    elif not valid_threshold(profile["false_positive_relation_rate_max"]):
        failures.append("profile_invalid:false_positive_relation_rate_max")
    elif fp_relation_rate > profile["false_positive_relation_rate_max"]:
        failures.append("false_positive_relation_rate_max")

    expected_categories = {
        norm(item.get("category", "")) for case in test_cases
        for item in case.get("gold", {}).get("attributes", [])
        if isinstance(item, dict) and norm(item.get("category", ""))
    }
    precision_floors = profile.get("category_precision_min", {})
    recall_floors = profile.get("category_recall_min", {})
    for label, floors in (("precision", precision_floors), ("recall", recall_floors)):
        if not isinstance(floors, dict) or not floors:
            failures.append("profile_missing:category_" + label + "_min")
            continue
        missing = sorted(expected_categories - set(floors))
        extra = sorted(set(floors) - expected_categories)
        if missing:
            failures.append("profile_category_" + label + "_missing:" + ",".join(missing))
        if extra:
            failures.append("profile_category_" + label + "_unexpected:" + ",".join(extra))
        for category, floor in floors.items():
            metric = category_metrics.get(category)
            if metric is None or supported_categories.get(category, 0) == 0:
                failures.append("category_without_gold_support:" + category)
            elif not valid_threshold(floor):
                failures.append("invalid_category_" + label + "_floor:" + category)
            elif metric[label] < floor:
                failures.append("category_" + label + ":" + category)

    for phenomenon, support in phenomenon_support.items():
        if support["positive_cases"] == 0 or support["negative_cases"] == 0:
            failures.append("phenomenon_insufficient_gold_support:" + phenomenon)
    for label in ("precision", "recall"):
        floors = profile.get("phenomenon_" + label + "_min", {})
        if not isinstance(floors, dict):
            failures.append("invalid_phenomenon_" + label + "_min")
            continue
        missing_phenomena = sorted(set(phenomenon_names) - set(floors))
        extra_phenomena = sorted(set(floors) - set(phenomenon_names))
        if missing_phenomena:
            failures.append("profile_phenomenon_" + label + "_missing:" + ",".join(missing_phenomena))
        if extra_phenomena:
            failures.append("profile_phenomenon_" + label + "_unexpected:" + ",".join(extra_phenomena))
        for phenomenon in phenomenon_names:
            floor = floors.get(phenomenon)
            metric = phenomenon_metrics[phenomenon]
            if floor is None:
                failures.append("profile_missing:phenomenon_" + label + ":" + phenomenon)
            elif not valid_threshold(floor):
                failures.append("invalid_phenomenon_" + label + ":" + phenomenon)
            elif metric[label] < floor:
                failures.append("phenomenon_" + label + ":" + phenomenon)

    return {
        "recognition_quality_gate": "PASS" if not failures else "FAIL",
        "failures": failures, "attribute_metrics": attr_metrics,
        "relation_metrics": rel_metrics, "category_metrics": category_metrics,
        "phenomenon_metrics": phenomenon_metrics,
        "phenomenon_gold_support": phenomenon_support,
        "category_gold_support": supported_categories,
        "macro_f1": macro_f1, "schema_valid_rate": schema_valid_rate,
        "evidence_support_rate": evidence_rate,
        "evidence_span_metrics": span_metrics,
        "relation_evidence_span_metrics": relation_span_metrics,
        "gold_evidence_span_count": span_gold_total,
        "predicted_evidence_span_count": sum(row["predicted_evidence_span_count"] for row in results),
        "duplicate_evidence_span_count": sum(row["duplicate_evidence_span_count"] for row in results),
        "false_positive_relation_rate": fp_relation_rate,
        "unsupported_attribute_count": sum(row["unsupported_attribute_count"] for row in results),
        "duplicate_attribute_count": sum(row["duplicate_attribute_count"] for row in results),
        "duplicate_relation_count": sum(row["duplicate_relation_count"] for row in results),
        "malformed_count": sum(row["malformed_count"] for row in results),
        "canonical_schema_error_count": sum(row["canonical_schema_error_count"] for row in results),
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
