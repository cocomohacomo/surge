#!/usr/bin/env python3
"""Fail-closed aggregate gate with immutable evidence identity checks."""
import json,sys
from pathlib import Path
def load(p): return json.loads(Path(p).read_text(encoding="utf-8"))
def gate(core,recognition,expected):
 required=("commit_sha","profile_sha256","corpus_sha256")
 identities={}
 for name,report in (("core_integrity",core),("recognition_quality",recognition)):
  identities[name]={k:report.get(k) for k in required}
 statuses={"core_integrity":core.get("core_integrity_gate",core.get("gate_status","NOT_RUN")),
 "recognition_quality":recognition.get("recognition_quality_gate","NOT_RUN")}
 problems=[]
 for name,vals in identities.items():
  for k,v in vals.items():
   if not v: problems.append(name+":missing_"+k)
   elif expected.get(k) and v!=expected[k]: problems.append(name+":mismatched_"+k)
 if identities["core_integrity"]!=identities["recognition_quality"]: problems.append("track_identity_mismatch")
 if any(v!="PASS" for v in statuses.values()): problems.extend(k+":"+v for k,v in statuses.items() if v!="PASS")
 passed=not problems and all(statuses.values())
 return {"aggregate_gate":"PASS" if passed else "FAIL","tracks":statuses,"identities":identities,"expected_identity":expected,"problems":sorted(set(problems)),"fail_closed":True}
def main():
 if len(sys.argv)!=5: raise SystemExit("usage: aggregate_quality_gate.py CORE.json RECOGNITION.json EXPECTED_IDENTITY.json OUTPUT.json")
 result=gate(load(sys.argv[1]),load(sys.argv[2]),load(sys.argv[3]))
 Path(sys.argv[4]).write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8"); print(json.dumps(result,indent=2))
 return 0 if result["aggregate_gate"]=="PASS" else 1
if __name__=="__main__": raise SystemExit(main())
