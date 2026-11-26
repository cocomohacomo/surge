#!/usr/bin/env python3
"""Structured attribute/relation/evidence scoring for SURGE recognition.

Gold corpus status must be independently adjudicated to qualify as a benchmark.
Synthetic seed data is intentionally reported as NOT_RUN for quality qualification.
"""
from __future__ import annotations
import hashlib, json, sys
from pathlib import Path
from typing import Any

def norm(value: Any) -> str:
    return " ".join(str(value).casefold().strip().split())

def score_counts(tp: int, fp: int, fn: int) -> dict:
    precision = tp / (tp + fp) if tp + fp else (1.0 if fn == 0 else 0.0)
    recall = tp / (tp + fn) if tp + fn else 1.0
    return {"tp": tp, "fp": fp, "fn": fn, "precision": precision, "recall": recall,
            "f1": 2*precision*recall/(precision+recall) if precision+recall else 0.0}

def evaluate_case(case: dict, prediction: dict) -> dict:
    source = norm(case.get("context", "") + " " + case.get("text", ""))
    gold = case.get("gold", {})
    gold_attributes = {(norm(a.get("key")), norm(a.get("value")), norm(a.get("polarity", "positive"))) for a in gold.get("attributes", [])}
    predictions = prediction.get("attributes", prediction.get("candidates", []))
    pred_attributes = set()
    malformed = 0
    evidence_flags = []
    for item in predictions:
        if not isinstance(item, dict):
            malformed += 1
            continue
        key = norm(item.get("key", item.get("attribute", "")))
        value = norm(item.get("value", item.get("text", "")))
        polarity = norm(item.get("polarity", "positive"))
        if not key or not value:
            malformed += 1
            continue
        pred_attributes.add((key, value, polarity))
        quote = norm(item.get("evidence_quote", item.get("evidence", "")))
        evidence_flags.append(bool(quote) and quote in source)
    attrs = score_counts(len(gold_attributes & pred_attributes), len(pred_attributes - gold_attributes) + malformed, len(gold_attributes - pred_attributes))
    gold_rel = {(norm(r.get("type")), norm(r.get("source")), norm(r.get("target"))) for r in gold.get("relations", [])}
    pred_rel = set()
    malformed_rel = 0
    for r in prediction.get("relations", []):
        if isinstance(r, dict) and all(isinstance(r.get(k), str) and r.get(k).strip() for k in ("type", "source", "target")):
            pred_rel.add((norm(r["type"]), norm(r["source"]), norm(r["target"])))
        else:
            malformed_rel += 1
            malformed += 1
    relations = score_counts(len(gold_rel & pred_rel), len(pred_rel - gold_rel) + malformed_rel, len(gold_rel - pred_rel)))
    return {"case_id":case["case_id"],"attribute_metrics":attrs,"relation_metrics":relations,
            "evidence_supported":sum(evidence_flags),"evidence_total":len(evidence_flags),
            "evidence_support_rate":sum(evidence_flags)/len(evidence_flags) if evidence_flags else 0.0,
            "malformed_prediction_count":malformed,"categories":case.get("categories",[])}

def evaluate(corpus: dict, predictions: dict, corpus_digest: str) -> dict:
    cases=corpus.get("cases", [])
    test=[c for c in cases if c.get("split")=="test"]
    annotation=corpus.get("annotation", {})
    if not test or annotation.get("independent_adjudication") is not True:
        return {"recognition_quality_gate":"NOT_RUN","reason":"held-out test corpus is empty or independent annotation/adjudication is not attested","case_count":len(test),"seed_cases":len(cases),"corpus_sha256":corpus_digest}
    results=[]
    for case in test:
        prediction=predictions.get(case["case_id"])
        if not isinstance(prediction,dict):
            return {"recognition_quality_gate":"FAIL","reason":"missing prediction for "+case["case_id"],"case_count":len(test)}
        results.append(evaluate_case(case,prediction))
    def aggregate(metric):
        tp=sum(r[metric]["tp"] for r in results); fp=sum(r[metric]["fp"] for r in results); fn=sum(r[metric]["fn"] for r in results)
        return score_counts(tp,fp,fn)
    attr=aggregate("attribute_metrics"); rel=aggregate("relation_metrics")
    evidence_total=sum(r["evidence_total"] for r in results); evidence_hits=sum(r["evidence_supported"] for r in results)
    evidence_rate=evidence_hits/evidence_total if evidence_total else 0.0
    profile=corpus.get("profile", {})
    gate="PASS" if attr["f1"]>=profile.get("attribute_f1_min",1.1) and rel["f1"]>=profile.get("relation_f1_min",1.1) and evidence_rate>=profile.get("evidence_support_min",1.1) else "FAIL"
    return {"recognition_quality_gate":gate,"attribute_metrics":attr,"relation_metrics":rel,"evidence_support_rate":evidence_rate,"case_count":len(results),"per_case":results,"corpus_sha256":corpus_digest,"profile":profile}

def main():
    if len(sys.argv)!=4: raise SystemExit("usage: evaluate_gold_corpus.py CORPUS.json PREDICTIONS.json OUTPUT.json")
    corpus_path=Path(sys.argv[1]); corpus=json.loads(corpus_path.read_text(encoding="utf-8"))
    preds=json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
    result=evaluate(corpus,preds,hashlib.sha256(corpus_path.read_bytes()).hexdigest())
    Path(sys.argv[3]).write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\\n",encoding="utf-8")
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if result["recognition_quality_gate"]=="PASS" else 1
if __name__=="__main__": raise SystemExit(main())
