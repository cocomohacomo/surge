#!/usr/bin/env python3
"""Precision/recall evaluation for structured SURGE Projector outputs.

The evaluator is deliberately external to Core. It measures agreement with a
small adjudicated challenge set; it does not infer truth or relevance.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


def _normalise(value: Any) -> str:
    return " ".join(str(value).casefold().split())


def _safe_list(value: Any, field: str, case_id: str) -> list:
    if not isinstance(value, list):
        raise ValueError(f"{case_id}: {field} must be an array")
    return value


def evaluate_case(result: dict) -> dict:
    case_id = str(result.get("case_id", ""))
    if not case_id:
        raise ValueError("case_id is required")
    parsed = result.get("parsed_output")
    valid_json = isinstance(parsed, dict)
    if not valid_json:
        parsed = {}

    candidates = parsed.get("candidates", [])
    relations = parsed.get("relations", [])
    structurally_valid = (
        isinstance(candidates, list)
        and isinstance(relations, list)
        and all(isinstance(item, dict) and isinstance(item.get("text"), str)
                for item in candidates)
        and all(isinstance(item, dict) and isinstance(item.get("type"), str)
                for item in relations)
    )
    if valid_json and not structurally_valid:
        raise ValueError(f"{case_id}: candidates/relations have invalid structure")
    if not structurally_valid:
        candidates, relations = [], []

    expected_candidates = _safe_list(
        result.get("expected_candidate_markers", []),
        "expected_candidate_markers", case_id)
    expected_relations = _safe_list(
        result.get("expected_relation_types", []),
        "expected_relation_types", case_id)
    if not all(isinstance(x, str) and x.strip() for x in expected_candidates):
        raise ValueError(f"{case_id}: expected candidate markers must be non-empty strings")
    if not all(isinstance(x, str) and x.strip() for x in expected_relations):
        raise ValueError(f"{case_id}: expected relation types must be non-empty strings")

    gold_candidates = [_normalise(x) for x in expected_candidates]
    predicted_candidates = [_normalise(x["text"]) for x in candidates]
    unmatched_gold = list(gold_candidates)
    candidate_tp = 0
    candidate_fp = 0
    matched_predictions = []
    for prediction in predicted_candidates:
        match_index = next((i for i, gold in enumerate(unmatched_gold)
                            if gold in prediction or prediction in gold), None)
        if match_index is None or not prediction:
            candidate_fp += 1
        else:
            candidate_tp += 1
            matched_predictions.append(candidates[len(matched_predictions)].get("text", ""))
            unmatched_gold.pop(match_index)
    candidate_fn = len(unmatched_gold)

    gold_relations = [_normalise(x) for x in expected_relations]
    predicted_relations = [_normalise(x["type"]) for x in relations]
    remaining_relations = list(gold_relations)
    relation_tp = relation_fp = 0
    for prediction in predicted_relations:
        if prediction in remaining_relations:
            relation_tp += 1
            remaining_relations.remove(prediction)
        else:
            relation_fp += 1
    relation_fn = len(remaining_relations)

    tp = candidate_tp + relation_tp
    fp = candidate_fp + relation_fp
    fn = candidate_fn + relation_fn
    precision = tp / (tp + fp) if tp + fp else (1.0 if not fn else 0.0)
    recall = tp / (tp + fn) if tp + fn else 1.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {
        "case_id": case_id,
        "json_valid": valid_json,
        "structure_valid": structurally_valid,
        "candidate_tp": candidate_tp,
        "candidate_fp": candidate_fp,
        "candidate_fn": candidate_fn,
        "relation_tp": relation_tp,
        "relation_fp": relation_fp,
        "relation_fn": relation_fn,
        "tp": tp,
        "fp": fp,
        "fn": fn,
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "candidate_count": len(candidates),
        "relation_count": len(relations),
    }


def _metrics(cases: list[dict]) -> dict:
    tp = sum(c["tp"] for c in cases)
    fp = sum(c["fp"] for c in cases)
    fn = sum(c["fn"] for c in cases)
    precision = tp / (tp + fp) if tp + fp else (1.0 if not fn else 0.0)
    recall = tp / (tp + fn) if tp + fn else 1.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {"tp": tp, "fp": fp, "fn": fn, "precision": precision,
            "recall": recall, "f1": f1}


def evaluate_report(report: dict) -> dict:
    if not isinstance(report, dict) or not isinstance(report.get("models"), list):
        raise ValueError("report.models must be an array")
    models = []
    for model in report["models"]:
        if not isinstance(model, dict) or not isinstance(model.get("results"), list):
            raise ValueError("each model must contain a results array")
        cases = [evaluate_case(item) for item in model["results"]]
        if not cases:
            raise ValueError(f"{model.get('name', '<unnamed>')}: no evaluation cases")
        metrics = _metrics(cases)
        models.append({
            "model": model.get("name"),
            "json_valid_cases": sum(c["json_valid"] for c in cases),
            "structure_valid_cases": sum(c["structure_valid"] for c in cases),
            "case_count": len(cases),
            **metrics,
            "cases": cases,
            "quality_gate": "PASS" if metrics["precision"] >= 0.50 and
                            metrics["recall"] >= 0.50 else "FAIL",
        })
    return {
        "evaluation": "surge-projector-semantic-prf-v0.2",
        "json_validity_is_independent": True,
        "matching": "casefolded whitespace-normalized substring for candidate text; exact relation type",
        "models": models,
        "limitations": [
            "This small synthetic challenge set is not a statistically valid Japanese benchmark.",
            "Candidate matching is lexical and does not recognize paraphrases or evidence entailment.",
            "A passing score does not establish truth, relevance, or general Japanese understanding.",
        ],
    }


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: evaluate_projector_semantics.py INPUT.json OUTPUT.json")
    report = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    result = evaluate_report(report)
    Path(sys.argv[2]).write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
