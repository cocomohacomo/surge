#!/usr/bin/env python3
"""Evaluate meeting-decision extraction against independently adjudicated gold JSONL."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import pathlib
import unicodedata


def read_jsonl(path: pathlib.Path) -> list[dict]:
    rows = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f"{path}:{line_no}: INVALID_JSON") from exc
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{line_no}: RECORD_NOT_OBJECT")
        rows.append(value)
    return rows


def key(value: str) -> str:
    return " ".join(unicodedata.normalize("NFC", value).casefold().split())


def index_unique(rows: list[dict], label: str) -> dict[str, dict]:
    result = {}
    for row in rows:
        case_id = row.get("case_id")
        if not isinstance(case_id, str) or not case_id or case_id in result:
            raise ValueError(f"{label}: CASE_ID_MISSING_OR_DUPLICATE")
        result[case_id] = row
    return result


def validate_span(item: dict, source: str, source_digest: str) -> bool:
    if item.get("source_sha256") != source_digest:
        return False
    start, end, quote = item.get("evidence_start"), item.get("evidence_end"), item.get("evidence_quote")
    if (isinstance(start, bool) or not isinstance(start, int) or
        isinstance(end, bool) or not isinstance(end, int) or
        not isinstance(quote, str) or start < 0 or end <= start):
        return False
    raw = unicodedata.normalize("NFC", source).encode("utf-8")
    try:
        return raw[start:end].decode("utf-8") == quote
    except (UnicodeDecodeError, IndexError):
        return False


def evaluate(cases: list[dict], gold_rows: list[dict], pred_rows: list[dict],
             min_coverage: float, min_critical_recall: float,
             max_selective_risk: float, min_evidence_grounding: float = 0.98) -> dict:
    case_map = index_unique(cases, "cases")
    gold_map = index_unique(gold_rows, "gold")
    pred_map = index_unique(pred_rows, "predictions")
    if set(case_map) != set(gold_map) or set(case_map) != set(pred_map):
        raise ValueError("CASE_SET_MISMATCH")
    tp = fp = fn = critical_tp = critical_fn = 0
    owner_ok = owner_n = deadline_ok = deadline_n = 0
    grounded = predicted_items = 0
    accepted_cases = exact_accepted_cases = review_cases = 0
    per_case = []
    for case_id in sorted(case_map):
        case, gold, pred = case_map[case_id], gold_map[case_id], pred_map[case_id]
        source = case.get("source_text")
        if not isinstance(source, str):
            raise ValueError(f"{case_id}: SOURCE_TEXT_MISSING")
        digest = hashlib.sha256(unicodedata.normalize("NFC", source).encode("utf-8")).hexdigest()
        if case.get("source_sha256") != digest or gold.get("source_sha256") != digest or pred.get("source_sha256") != digest:
            raise ValueError(f"{case_id}: SOURCE_DIGEST_MISMATCH")
        status = pred.get("decision")
        if status not in ("ACCEPT", "REVIEW", "ABSTAIN"):
            raise ValueError(f"{case_id}: INVALID_DECISION")
        if status == "REVIEW":
            review_cases += 1
        g_items, p_items = gold.get("items"), pred.get("items")
        if not isinstance(g_items, list) or not isinstance(p_items, list):
            raise ValueError(f"{case_id}: ITEMS_NOT_ARRAY")
        g, p = {}, {}
        for item in g_items:
            action = item.get("action") if isinstance(item, dict) else None
            if not isinstance(action, str) or not action or key(action) in g:
                raise ValueError(f"{case_id}: GOLD_ACTION_MISSING_OR_DUPLICATE")
            g[key(action)] = item
        for item in p_items:
            action = item.get("action") if isinstance(item, dict) else None
            if not isinstance(action, str) or not action or key(action) in p:
                raise ValueError(f"{case_id}: PRED_ACTION_MISSING_OR_DUPLICATE")
            p[key(action)] = item
        if status != "ACCEPT":
            p = {}
        if status == "ACCEPT":
            accepted_cases += 1
        matches = set(g) & set(p)
        case_tp, case_fp, case_fn = len(matches), len(set(p)-set(g)), len(set(g)-set(p))
        tp += case_tp
        fp += case_fp
        fn += case_fn
        for action, item in g.items():
            if item.get("critical") is True:
                if action in p:
                    critical_tp += 1
                else:
                    critical_fn += 1
        for action in matches:
            expected, actual = g[action], p[action]
            if expected.get("owner") is not None:
                owner_n += 1
                owner_ok += expected.get("owner") == actual.get("owner")
            if expected.get("deadline") is not None:
                deadline_n += 1
                deadline_ok += expected.get("deadline") == actual.get("deadline")
        for item in p.values():
            predicted_items += 1
            grounded += validate_span(item, source, digest)
        case_exact = (set(g) == set(p) and all(
            g[a].get("owner") == p[a].get("owner") and
            g[a].get("deadline") == p[a].get("deadline") and
            validate_span(p[a], source, digest) for a in g))
        if status == "ACCEPT" and case_exact:
            exact_accepted_cases += 1
        per_case.append({"case_id": case_id, "decision": status, "tp": case_tp,
                         "fp": case_fp, "fn": case_fn, "exact": bool(case_exact)})
    precision = tp / (tp + fp) if tp + fp else None
    recall = tp / (tp + fn) if tp + fn else None
    f1 = 2*precision*recall/(precision+recall) if precision is not None and recall is not None and precision+recall else None
    coverage = accepted_cases / len(case_map) if case_map else None
    critical_recall = critical_tp / (critical_tp + critical_fn) if critical_tp + critical_fn else None
    selective_risk = ((accepted_cases-exact_accepted_cases)/accepted_cases) if accepted_cases else None
    evidence_rate = grounded / predicted_items if predicted_items else None
    if (not case_map or coverage is None or critical_recall is None or
        selective_risk is None or evidence_rate is None):
        gate = "INCONCLUSIVE"
    else:
        gate = "PASS" if (coverage >= min_coverage and
                          critical_recall >= min_critical_recall and
                          selective_risk <= max_selective_risk and
                          evidence_rate >= min_evidence_grounding) else "FAIL"
    return {
        "benchmark": "SURGE_MEETING_DECISIONS_V0.1",
        "case_count": len(case_map), "accepted_cases": accepted_cases, "review_cases": review_cases,
        "abstained_cases": len(case_map)-accepted_cases-review_cases,
        "micro": {"tp": tp, "fp": fp, "fn": fn, "precision": precision, "recall": recall, "f1": f1},
        "critical_recall": critical_recall, "owner_accuracy": owner_ok/owner_n if owner_n else None,
        "deadline_accuracy": deadline_ok/deadline_n if deadline_n else None,
        "evidence_grounding_rate": evidence_rate, "coverage": coverage,
        "selective_risk": selective_risk,
        "thresholds": {"minimum_coverage": min_coverage,
                       "minimum_critical_recall": min_critical_recall,
                       "maximum_selective_risk": max_selective_risk,
                       "minimum_evidence_grounding": min_evidence_grounding},
        "gate": gate, "per_case": per_case,
        "note": "INDEPENDENT_GOLD_REQUIRED; a structurally valid run is not evidence of model quality."
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", required=True, type=pathlib.Path)
    parser.add_argument("--gold", required=True, type=pathlib.Path)
    parser.add_argument("--predictions", required=True, type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--min-coverage", type=float, default=0.70)
    parser.add_argument("--min-critical-recall", type=float, default=0.90)
    parser.add_argument("--max-selective-risk", type=float, default=0.10)
    parser.add_argument("--min-evidence-grounding", type=float, default=0.98)
    args = parser.parse_args()
    for value in (args.min_coverage, args.min_critical_recall,
                  args.max_selective_risk, args.min_evidence_grounding):
        if not math.isfinite(value) or not 0 <= value <= 1:
            parser.error("thresholds must be finite fractions in [0,1]")
    result = evaluate(read_jsonl(args.cases), read_jsonl(args.gold),
                      read_jsonl(args.predictions), args.min_coverage,
                      args.min_critical_recall, args.max_selective_risk,
                      args.min_evidence_grounding)
    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    if result["gate"] == "FAIL":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
