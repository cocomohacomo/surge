#!/usr/bin/env python3
"""Strict structured semantic evaluator. Synthetic/unadjudicated corpora never qualify."""
from __future__ import annotations
import hashlib, json, sys
from pathlib import Path

def norm(v): return " ".join(str(v).casefold().strip().split())
def counts(tp,fp,fn):
 p=tp/(tp+fp) if tp+fp else (1.0 if fn==0 else 0.0); r=tp/(tp+fn) if tp+fn else 1.0
 return {"tp":tp,"fp":fp,"fn":fn,"precision":p,"recall":r,"f1":2*p*r/(p+r) if p+r else 0.0}

def evaluate_case(case,pred):
 source=norm(case.get("context","")+" "+case.get("text","")); gold=case.get("gold",{})
 ga={(norm(x["key"]),norm(x["value"]),norm(x.get("polarity","positive"))) for x in gold.get("attributes",[])}
 pa=[]; malformed=0; evidence_total=0; evidence_supported=0; unsupported=0
 raw=pred.get("attributes",pred.get("candidates",[]))
 if not isinstance(raw,list): raw=[]; malformed+=1
 for x in raw:
  if not isinstance(x,dict) or not norm(x.get("key",x.get("attribute",""))) or not norm(x.get("value",x.get("text",""))):
   malformed+=1; continue
  item=(norm(x.get("key",x.get("attribute",""))),norm(x.get("value",x.get("text",""))),norm(x.get("polarity","positive")))
  pa.append(item)
  quote=norm(x.get("evidence_quote",x.get("evidence","")))
  evidence_total+=1
  if quote and quote in source: evidence_supported+=1
  else: unsupported+=1
 unique=set(pa); duplicate_count=len(pa)-len(unique)
 am=counts(len(ga&unique),len(unique-ga)+malformed+duplicate_count,len(ga-unique))
 gr={(norm(x["type"]),norm(x["source"]),norm(x["target"])) for x in gold.get("relations",[])}
 pr=[]; rel_malformed=0
 relraw=pred.get("relations",[])
 if not isinstance(relraw,list): relraw=[]; rel_malformed+=1
 for x in relraw:
  if not isinstance(x,dict) or not all(isinstance(x.get(k),str) and x[k].strip() for k in ("type","source","target")):
   rel_malformed+=1; continue
  pr.append((norm(x["type"]),norm(x["source"]),norm(x["target"])))
 rel_unique=set(pr); rel_dups=len(pr)-len(rel_unique)
 rm=counts(len(gr&rel_unique),len(rel_unique-gr)+rel_malformed+rel_dups,len(gr-rel_unique))
 cats={}
 for cat in case.get("categories",[]):
  cga={(norm(x["key"]),norm(x["value"]),norm(x.get("polarity","positive"))) for x in gold.get("attributes",[]) if x.get("category")==cat}
  cpa={item for item in unique if item in cga}
  # Gold category positives vs all predicted attributes assigned to this category.
  assigned={ (norm(x.get("key",x.get("attribute",""))),norm(x.get("value",x.get("text",""))),norm(x.get("polarity","positive"))) for x in raw if isinstance(x,dict) and x.get("category")==cat and norm(x.get("key",x.get("attribute",""))) and norm(x.get("value",x.get("text",""))) }
  cats[cat]=counts(len(cga&assigned),len(assigned-cga)+sum(1 for x in raw if isinstance(x,dict) and x.get("category")==cat and not norm(x.get("key",x.get("attribute","")))),len(cga-assigned))
 return {"case_id":case["case_id"],"attribute":am,"relation":rm,"categories":cats,
 "duplicate_attribute_count":duplicate_count,"duplicate_relation_count":rel_dups,
 "malformed_count":malformed+rel_malformed,"unsupported_attribute_count":unsupported,
 "evidence_total":evidence_total,"evidence_supported":evidence_supported}

def evaluate(corpus,predictions,corpus_sha,profile_sha=None,commit_sha=None):
 test=[c for c in corpus.get("cases",[]) if c.get("split")=="test"]
 ann=corpus.get("annotation",{})
 if not test or ann.get("independent_adjudication") is not True:
  return {"recognition_quality_gate":"NOT_RUN","reason":"no independently adjudicated held-out test corpus","case_count":len(test),"corpus_sha256":corpus_sha,"profile_sha256":profile_sha,"commit_sha":commit_sha}
 results=[]
 for c in test:
  p=predictions.get(c["case_id"])
  if not isinstance(p,dict): return {"recognition_quality_gate":"FAIL","reason":"missing prediction: "+c["case_id"],"case_count":len(test),"corpus_sha256":corpus_sha,"profile_sha256":profile_sha,"commit_sha":commit_sha}
  results.append(evaluate_case(c,p))
 def aggregate(field):
  return counts(sum(x[field]["tp"] for x in results),sum(x[field]["fp"] for x in results),sum(x[field]["fn"] for x in results))
 attr=aggregate("attribute"); rel=aggregate("relation")
 category_names=sorted({k for x in results for k in x["categories"]})
 category_metrics={}
 for cat in category_names:
  vals=[x["categories"][cat] for x in results if cat in x["categories"]]
  category_metrics[cat]=counts(sum(v["tp"] for v in vals),sum(v["fp"] for v in vals),sum(v["fn"] for v in vals))
 evn=sum(x["evidence_total"] for x in results); evs=sum(x["evidence_supported"] for x in results)
 profile=corpus.get("profile",{})
 failures=[]
 for key,metric in (("attribute_f1_min",attr),("relation_f1_min",rel)):
  if metric["f1"]<profile.get(key,1.1): failures.append(key)
 if (evs/evn if evn else 0.0) < profile.get("evidence_support_min",1.1): failures.append("evidence_support_min")
 for cat,m in category_metrics.items():
  floor=profile.get("category_recall_min",{}).get(cat)
  if floor is None or m["recall"]<floor: failures.append("category_recall:"+cat)
 return {"recognition_quality_gate":"PASS" if not failures else "FAIL","failures":failures,"attribute_metrics":attr,"relation_metrics":rel,"category_metrics":category_metrics,
 "evidence_support_rate":evs/evn if evn else 0.0,"unsupported_attribute_count":sum(x["unsupported_attribute_count"] for x in results),
 "duplicate_attribute_count":sum(x["duplicate_attribute_count"] for x in results),"duplicate_relation_count":sum(x["duplicate_relation_count"] for x in results),
 "malformed_count":sum(x["malformed_count"] for x in results),"case_count":len(results),"per_case":results,
 "corpus_sha256":corpus_sha,"profile_sha256":profile_sha,"commit_sha":commit_sha}

def main():
 if len(sys.argv)!=4: raise SystemExit("usage: evaluate_gold_corpus.py CORPUS.json PREDICTIONS.json OUTPUT.json")
 cp=Path(sys.argv[1]); corpus=json.loads(cp.read_text(encoding="utf-8")); preds=json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
 profile=json.dumps(corpus.get("profile",{}),sort_keys=True,separators=(",",":")).encode()
 result=evaluate(corpus,preds,hashlib.sha256(cp.read_bytes()).hexdigest(),hashlib.sha256(profile).hexdigest())
 Path(sys.argv[3]).write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8"); print(json.dumps(result,ensure_ascii=False,indent=2))
 return 0 if result["recognition_quality_gate"]=="PASS" else 1
if __name__=="__main__": raise SystemExit(main())
