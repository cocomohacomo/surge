#!/usr/bin/env python3
"""Independent semantic feature evaluation for SURGE Projector outputs.

This evaluates structured candidate/relation coverage separately from JSON
parseability. It is a transparent benchmark gate, not a truth classifier.
"""
from __future__ import annotations
import json
import sys
from pathlib import Path

def evaluate_case(result: dict) -> dict:
    parsed = result.get("parsed_output")
    candidates = parsed.get("candidates", []) if isinstance(parsed, dict) else []
    relations = parsed.get("relations", []) if isinstance(parsed, dict) else []
    candidate_text = " ".join(
        str(c.get("text", "")) for c in candidates if isinstance(c, dict)
    )
    relation_types = {
        str(r.get("type", "")) for r in relations if isinstance(r, dict)
    }
    expected_markers = result.get("expected_candidate_markers", [])
    expected_relations = result.get("expected_relation_types", [])
    candidate_hits = [m for m in expected_markers if m in candidate_text]
    relation_hits = [r for r in expected_relations if r in relation_types]
    expected_total = len(expected_markers) + len(expected_relations)
    hit_total = len(candidate_hits) + len(relation_hits)
    return {
        "case_id": result.get("case_id"),
        "json_valid": isinstance(parsed, dict),
        "candidate_count": len(candidates) if isinstance(candidates, list) else 0,
        "relation_count": len(relations) if isinstance(relations, list) else 0,
        "candidate_marker_hits": candidate_hits,
        "candidate_marker_total": len(expected_markers),
        "relation_type_hits": relation_hits,
        "relation_type_total": len(expected_relations),
        "semantic_feature_hits": hit_total,
        "semantic_feature_total": expected_total,
        "semantic_feature_recall": hit_total / expected_total if expected_total else 1.0,
    }

def evaluate_report(report: dict) -> dict:
    models = []
    for model in report.get("models", []):
        cases = [evaluate_case(r) for r in model.get("results", [])]
        total = sum(c["semantic_feature_total"] for c in cases)
        hits = sum(c["semantic_feature_hits"] for c in cases)
        models.append({
            "model": model.get("name"),
            "json_valid_cases": sum(c["json_valid"] for c in cases),
            "case_count": len(cases),
            "semantic_feature_hits": hits,
            "semantic_feature_total": total,
            "semantic_feature_recall": hits / total if total else 1.0,
            "cases": cases,
            "gate": "PASS" if total and hits == total else "FAIL",
        })
    return {
        "evaluation": "surge-projector-semantic-features-v0.1",
        "json_validity_is_independent": True,
        "models": models,
        "limitations": [
            "Feature-marker recall is not full semantic precision/recall.",
            "Expected markers and relation labels are a small hand-authored challenge set.",
            "A passing result does not establish truth, relevance, or general Japanese understanding.",
        ],
    }

def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: evaluate_projector_semantics.py INPUT.json OUTPUT.json")
    report = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    result = evaluate_report(report)
    Path(sys.argv[2]).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
