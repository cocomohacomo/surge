#!/usr/bin/env python3
"""Pinned CPU model matrix for structured attribute/relation/evidence output.

Uses the seed corpus for engineering smoke metrics only. Since its annotation is
not independent, semantic qualification is explicitly NOT_RUN.
"""
import hashlib, json, os, platform, re, shutil, subprocess, time
import sys
from pathlib import Path
from statistics import median
from huggingface_hub import hf_hub_download

# Direct script execution sets sys.path[0] to tools/, not the repository root.
# Import the sibling module without requiring tools/ to be a Python package.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from evaluate_gold_corpus import evaluate_case
from validate_projector_output import validate as validate_projector_output

REPO_ROOT=Path(__file__).resolve().parents[1]
LOCK_PATH=Path(os.environ.get("SURGE_MODEL_MATRIX_LOCK","config/model-matrix-lock-v1.0.json"))
if not LOCK_PATH.is_absolute():
 LOCK_PATH=REPO_ROOT/LOCK_PATH
LOCK=json.loads(LOCK_PATH.read_text(encoding="utf-8"))
MODELS=LOCK["models"]
ROOT=Path(os.environ.get("SURGE_MODEL_MATRIX_DIR","model-matrix"))
OUT=Path(os.environ.get("SURGE_MODEL_MATRIX_REPORT",ROOT/"model-matrix.json"))
CLI=os.environ.get("LLAMA_CLI","llama-cli")
THREADS=max(1,int(os.environ.get("SURGE_MODEL_THREADS","2")))
LLAMA_CPP_COMMIT=os.environ.get("LLAMA_CPP_COMMIT","")
LOCKED_LLAMA_COMMIT=LOCK["llama_cpp"]["commit"]
if not re.fullmatch(r"[0-9a-f]{40}",LOCKED_LLAMA_COMMIT) or LLAMA_CPP_COMMIT != LOCKED_LLAMA_COMMIT:
 raise SystemExit("LLAMA_CPP_COMMIT must match the full immutable commit in the model lock")
SYSTEM="""SURGEの外部認識Projectorとして、真偽を決定せず入力から属性と関係を抽出する。
Canonical JSON Schemaに完全準拠するJSONオブジェクトのみ出力する。
トップレベル必須: schema_version=\"surge-projector-output-v1.0\", attributes配列, relations配列。
属性必須: key,value,polarity(positive|negative|uncertain|unknown),category,evidence_spans配列。
関係必須: type,source,target,evidence_spans配列。
Evidence Span必須: source(context|text),start,end,quote。start/endは各原文のUnicodeコードポイント位置、endは排他的。quoteはsource文字列の[start:end]と完全一致させる。
判断保留はabstentions配列にreasonを記録する。根拠のない値は作らない。余分なキーやMarkdownは禁止。"""
PROMPT_TEMPLATE="<|im_start|>system\n{system}<|im_end|>\n<|im_start|>user\nContext: {context}\nInput: {text}\nReturn exactly one JSON object and no explanation. {mode_hint}<|im_end|>\n<|im_start|>assistant\n"

def sha256_file(path):
 h=hashlib.sha256()
 with path.open("rb") as stream:
  for chunk in iter(lambda: stream.read(1024 * 1024), b""):
   h.update(chunk)
 return h.hexdigest()

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
 prompt=PROMPT_TEMPLATE.format(system=SYSTEM,context=case.get("context",""),text=case.get("text",""),mode_hint="/no_think" if model.get("format") == "chatml_no_think" else "")
 cmd=["/usr/bin/time","-v",CLI,"-m",str(model_path),"-p",prompt,"-n","512","--temp","0","--top-k","1","--threads",str(THREADS),"--no-display-prompt","--simple-io"]
 t=time.perf_counter()
 try: proc=subprocess.run(cmd,capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=240)
 except subprocess.TimeoutExpired: return {"case_id":case["case_id"],"timeout":True,"elapsed_seconds":240}
 elapsed=time.perf_counter()-t; parsed=extract_json(proc.stdout)
 rss=re.findall(r"Maximum resident set size \(kbytes\):\s*(\d+)",proc.stderr)
 return {"case_id":case["case_id"],"return_code":proc.returncode,"elapsed_seconds":elapsed,"peak_rss_kib":int(rss[-1]) if rss else None,"json_valid":parsed is not None,"parsed_output":parsed,"raw_output":proc.stdout[:4000],"stderr":proc.stderr[-1000:]}

def main():
 if not shutil.which(CLI): raise SystemExit(f"llama-cli missing: {CLI}")
 corpus_path=Path(os.environ.get("SURGE_GOLD_CORPUS","tests/fixtures/semantic/gold_seed_v0.1.json"))
 if not corpus_path.is_absolute():
  corpus_path=REPO_ROOT/corpus_path
 corpus=json.loads(corpus_path.read_text(encoding="utf-8"))
 ROOT.mkdir(parents=True,exist_ok=True)
 cli_version=subprocess.run([CLI,"--version"],capture_output=True,text=True,timeout=30)
 if cli_version.returncode != 0:
  raise SystemExit("llama-cli --version failed")
 corpus_digest=hashlib.sha256(corpus_path.read_bytes()).hexdigest()
 profile_digest=hashlib.sha256(json.dumps(corpus.get("profile",{}),sort_keys=True,separators=(",",":")).encode()).hexdigest()
 report={"matrix":"surge-structured-projector-matrix-v1.0","corpus_id":corpus.get("corpus_id"),
  "corpus_status":"SEED_NOT_INDEPENDENT","qualification_gate":"NOT_RUN",
  "expected_model_count":len(MODELS),"expected_case_ids":[case["case_id"] for case in corpus.get("cases",[])],
  "corpus_sha256":corpus_digest,"profile_sha256":profile_digest,
  "experiment_manifest":{"manifest_version":"surge-experiment-manifest-v1.0",
   "source_commit":os.environ.get("GITHUB_SHA","unknown"),"llama_cpp_repository":LOCK["llama_cpp"]["repository"],
   "llama_cpp_commit":LLAMA_CPP_COMMIT,"model_lock_sha256":sha256_file(LOCK_PATH),"llama_cli_path":str(Path(CLI).resolve()),
   "runtime":{"python":sys.version,"platform":platform.platform(),"machine":platform.machine(),
              "processor":platform.processor(),"huggingface_hub":__import__("huggingface_hub").__version__,
              "llama_cli_version":(cli_version.stdout or cli_version.stderr).strip()},
   "threads":THREADS,"generation":{"max_tokens":512,"temperature":0,"top_k":1,"timeout_seconds":240},
   "system_prompt_sha256":hashlib.sha256(SYSTEM.encode()).hexdigest(),
   "prompt_template_sha256":hashlib.sha256(PROMPT_TEMPLATE.encode()).hexdigest(),
   "corpus_sha256":corpus_digest,"profile_sha256":profile_digest,
   "models":[{"repository":m["repo"],"revision":m["revision"],"tokenizer_repository":m["tokenizer_repo"],
              "tokenizer_revision":m["tokenizer_revision"],"file":m["file"]} for m in MODELS]},
  "models":[]}
 for m in MODELS:
  dest=ROOT/m["name"];dest.mkdir(parents=True,exist_ok=True)
  path=Path(hf_hub_download(repo_id=m["repo"],filename=m["file"],revision=m["revision"],local_dir=str(dest)))
  mr={"name":m["name"],"repository":m["repo"],"repository_revision":m["revision"],
      "tokenizer_repository":m["tokenizer_repo"],"tokenizer_revision":m["tokenizer_revision"],
      "tokenizer_source":"embedded GGUF tokenizer metadata; same immutable repository revision",
      "model_file":m["file"],"file_bytes":path.stat().st_size,"file_sha256":sha256_file(path),"results":[]}
  for case in corpus["cases"]:
   r=run(m,path,case)
   if r.get("return_code",0)!=0 and not r.get("timeout"): raise RuntimeError(f"model command failed: {m['name']} {case['case_id']}")
   if r.get("parsed_output") is not None:
    schema_errors=validate_projector_output(r["parsed_output"])
    r["projector_schema_valid"]=not schema_errors
    r["projector_schema_errors"]=schema_errors
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
