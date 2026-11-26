#!/usr/bin/env python3
"""Pinned CPU model matrix for structured attribute/relation/evidence output.

Uses the seed corpus for engineering smoke metrics only. Since its annotation is
not independent, semantic qualification is explicitly NOT_RUN.
"""
import json, os, re, shutil, subprocess, time
import sys
from pathlib import Path
from statistics import median
from huggingface_hub import hf_hub_download

# Direct script execution sets sys.path[0] to tools/, not the repository root.
# Import the sibling module without requiring tools/ to be a Python package.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from evaluate_gold_corpus import evaluate_case

MODELS=[
 {"name":"qwen3-0.6b-q4_k_m","repo":"gvij/qwen3-0.6b-gguf","file":"qwen3-0.6b-q4_k_m.gguf","format":"chatml"},
 {"name":"smollm2-135m-instruct-q4_k_m","repo":"bartowski/SmolLM2-135M-Instruct-GGUF","file":"SmolLM2-135M-Instruct-Q4_K_M.gguf","format":"chatml"},
]
ROOT=Path(os.environ.get("SURGE_MODEL_MATRIX_DIR","model-matrix"))
OUT=Path(os.environ.get("SURGE_MODEL_MATRIX_REPORT",ROOT/"model-matrix.json"))
CLI=os.environ.get("LLAMA_CLI","llama-cli")
THREADS=max(1,int(os.environ.get("SURGE_MODEL_THREADS","2")))
SYSTEM="""SURGEの外部認識Projectorとして、真偽を決定せず入力から属性と関係を抽出する。
JSONのみ出力: {"attributes":[{"key":"属性名","value":"値","polarity":"positive|negative|uncertain","evidence_quote":"原文抜粋"}],"relations":[{"type":"supports|contradicts|supersedes|refers_to|exception_of","source":"属性/対象","target":"属性/対象"}],"uncertainties":["曖昧な点"]}。
否定表現、矛盾、時間更新、曖昧性を明示し、根拠がない値は作らない。"""

def extract_json(text):
 decoder=json.JSONDecoder()
 for i,c in enumerate(text):
  if c=="{":
   try:
    v,_=decoder.raw_decode(text[i:])
    if isinstance(v,dict): return v
   except json.JSONDecodeError: pass
 return None

def run(model,model_path,case):
 prompt=f"<|im_start|>system\n{SYSTEM}<|im_end|>\n<|im_start|>user\nContext: {case.get('context','')}\nInput: {case.get('text','')}\nReturn structured JSON.<|im_end|>\n<|im_start|>assistant\n"
 cmd=["/usr/bin/time","-v",CLI,"-m",str(model_path),"-p",prompt,"-n","192","--temp","0","--top-k","1","--threads",str(THREADS),"--no-display-prompt","--simple-io"]
 t=time.perf_counter()
 try: proc=subprocess.run(cmd,capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=240)
 except subprocess.TimeoutExpired: return {"case_id":case["case_id"],"timeout":True,"elapsed_seconds":240}
 elapsed=time.perf_counter()-t; parsed=extract_json(proc.stdout)
 rss=re.findall(r"Maximum resident set size \(kbytes\):\s*(\d+)",proc.stderr)
 return {"case_id":case["case_id"],"return_code":proc.returncode,"elapsed_seconds":elapsed,"peak_rss_kib":int(rss[-1]) if rss else None,"json_valid":parsed is not None,"parsed_output":parsed,"raw_output":proc.stdout[:4000],"stderr":proc.stderr[-1000:]}

def main():
 if not shutil.which(CLI): raise SystemExit(f"llama-cli missing: {CLI}")
 corpus_path=Path(os.environ.get("SURGE_GOLD_CORPUS","tests/fixtures/semantic/gold_seed_v0.1.json"))
 corpus=json.loads(corpus_path.read_text(encoding="utf-8"))
 ROOT.mkdir(parents=True,exist_ok=True)
 report={"matrix":"surge-structured-projector-matrix-v0.1","corpus_id":corpus.get("corpus_id"),"corpus_status":"SEED_NOT_INDEPENDENT","qualification_gate":"NOT_RUN","models":[]}
 for m in MODELS:
  dest=ROOT/m["name"];dest.mkdir(parents=True,exist_ok=True)
  path=Path(hf_hub_download(repo_id=m["repo"],filename=m["file"],local_dir=str(dest)))
  mr={"name":m["name"],"file_bytes":path.stat().st_size,"results":[]}
  for case in corpus["cases"]:
   r=run(m,path,case)
   if r.get("return_code",0)!=0 and not r.get("timeout"): raise RuntimeError(f"model command failed: {m['name']} {case['case_id']}")
   if r.get("parsed_output") is not None:
    r["engineering_semantic_metrics"]=evaluate_case(case,r["parsed_output"])
   mr["results"].append(r)
  timed=sum(bool(r.get("timeout")) for r in mr["results"])
  elapsed=[r["elapsed_seconds"] for r in mr["results"] if "elapsed_seconds" in r]
  rss=[r["peak_rss_kib"] for r in mr["results"] if r.get("peak_rss_kib") is not None]
  mr.update({"json_valid_cases":sum(bool(r.get("json_valid")) for r in mr["results"]),"coverage":sum(bool(r.get("parsed_output")) for r in mr["results"])/len(mr["results"]) if mr["results"] else 0.0,"timeouts":timed,"latency_mean_seconds":sum(elapsed)/len(elapsed) if elapsed else None,"latency_p50_seconds":median(elapsed) if elapsed else None,"latency_max_seconds":max(elapsed) if elapsed else None,"peak_rss_kib_max":max(rss) if rss else None})
  report["models"].append(mr)
 OUT.parent.mkdir(parents=True,exist_ok=True); OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
 print(json.dumps(report,ensure_ascii=False,indent=2))
 return 0
if __name__=="__main__": raise SystemExit(main())
