#!/usr/bin/env python3
"""Fail-closed aggregate gate for Core Integrity and Recognition Quality evidence."""
import json, sys
from pathlib import Path

def load(path):
 return json.loads(Path(path).read_text(encoding="utf-8"))
def gate(core, recognition):
 statuses={"core_integrity":core.get("gate_status",core.get("core_integrity_gate","NOT_RUN")),"recognition_quality":recognition.get("recognition_quality_gate",recognition.get("gate_status","NOT_RUN"))}
 overall="PASS" if all(s=="PASS" for s in statuses.values()) else "FAIL"
 return {"aggregate_gate":overall,"tracks":statuses,"blocked_tracks":[k for k,v in statuses.items() if v!="PASS"],"fail_closed":True}
def main():
 if len(sys.argv)!=4: raise SystemExit("usage: aggregate_quality_gate.py CORE_REPORT.json RECOGNITION_REPORT.json OUTPUT.json")
 result=gate(load(sys.argv[1]),load(sys.argv[2]))
 Path(sys.argv[3]).write_text(json.dumps(result,indent=2)+"\\n",encoding="utf-8")
 print(json.dumps(result,indent=2))
 return 0 if result["aggregate_gate"]=="PASS" else 1
if __name__=="__main__": raise SystemExit(main())
