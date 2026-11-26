#!/usr/bin/env python3
"""Strictly summarize structured model-matrix outputs; execution != qualification."""
import json,sys
from pathlib import Path

def summarize(report):
 models=[]
 for model in report.get("models",[]):
  rows=model.get("results",[]); invalid=sum(not bool(r.get("json_valid")) for r in rows)
  timeouts=sum(bool(r.get("timeout")) for r in rows)
  missing_rss=sum(r.get("peak_rss_kib") is None for r in rows if not r.get("timeout"))
  semantic=[r["engineering_semantic_metrics"] for r in rows if isinstance(r.get("engineering_semantic_metrics"),dict)]
  def agg(k):
   tp=sum(x[k]["tp"] for x in semantic); fp=sum(x[k]["fp"] for x in semantic); fn=sum(x[k]["fn"] for x in semantic)
   p=tp/(tp+fp) if tp+fp else 0.; r=tp/(tp+fn) if tp+fn else 0.
   return {"tp":tp,"fp":fp,"fn":fn,"precision":p,"recall":r,"f1":2*p*r/(p+r) if p+r else 0.}
  models.append({"name":model.get("name"),"cases":len(rows),"coverage":len(semantic)/len(rows) if rows else 0.,
   "invalid_json":invalid,"timeouts":timeouts,"missing_rss":missing_rss,
   "attribute":agg("attribute_metrics") if semantic else None,"relation":agg("relation_metrics") if semantic else None,
   "unsupported_attributes":sum(x.get("unsupported_attribute_count",0) for x in semantic),
   "duplicate_attributes":sum(x.get("duplicate_attribute_count",0) for x in semantic),
   "duplicate_relations":sum(x.get("duplicate_relation_count",0) for x in semantic),
   "malformed":sum(x.get("malformed_count",0) for x in semantic),
   "execution_gate":"PASS" if rows and not invalid and not timeouts and not missing_rss else "FAIL"})
 return {"model_matrix_execution_gate":"PASS" if models and all(m["execution_gate"]=="PASS" for m in models) else "FAIL",
  "recognition_quality_gate":"NOT_RUN" if report.get("corpus_status")=="SEED_NOT_INDEPENDENT" else "PENDING",
  "models":models,"corpus_status":report.get("corpus_status")}
def main():
 if len(sys.argv)!=3: raise SystemExit("usage: summarize_model_matrix.py INPUT.json OUTPUT.json")
 result=summarize(json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")))
 Path(sys.argv[2]).write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
 print(json.dumps(result,ensure_ascii=False,indent=2))
 return 0 if result["model_matrix_execution_gate"]=="PASS" else 1
if __name__=="__main__": raise SystemExit(main())
