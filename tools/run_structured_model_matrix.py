#!/usr/bin/env python3
"""Pinned CPU model matrix with constrained decoding and strict post-validation.

The synthetic seed corpus is engineering-only. Recognition qualification remains
NOT_RUN until independently adjudicated held-out data is supplied.
"""
import hashlib, json, os, platform, re, shutil, subprocess, time
import sys
from pathlib import Path
from statistics import median
from huggingface_hub import hf_hub_download

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from evaluate_gold_corpus import evaluate_case
from validate_projector_output import validate as validate_projector_output

LOCK_PATH = Path(os.environ.get("SURGE_MODEL_MATRIX_LOCK", "config/model-matrix-lock-v1.0.json"))
if not LOCK_PATH.is_absolute():
    LOCK_PATH = REPO_ROOT / LOCK_PATH
LOCK = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
ALL_MODELS = LOCK["models"]
MODELS = [model for model in ALL_MODELS if model.get("enabled", True)]
EXCLUDED_MODELS = [
    {"name": model["name"], "reason": model.get("exclusion_reason", "disabled in model lock")}
    for model in ALL_MODELS if model not in MODELS
]
ROOT = Path(os.environ.get("SURGE_MODEL_MATRIX_DIR", "model-matrix"))
OUT = Path(os.environ.get("SURGE_MODEL_MATRIX_REPORT", ROOT / "model-matrix.json"))
CLI = os.environ.get("LLAMA_CLI", "llama-cli")
THREADS = max(1, int(os.environ.get("SURGE_MODEL_THREADS", "2")))
LLAMA_CPP_COMMIT = os.environ.get("LLAMA_CPP_COMMIT", "")
LOCKED_LLAMA_COMMIT = LOCK["llama_cpp"]["commit"]
if not re.fullmatch(r"[0-9a-f]{40}", LOCKED_LLAMA_COMMIT) or LLAMA_CPP_COMMIT != LOCKED_LLAMA_COMMIT:
    raise SystemExit("LLAMA_CPP_COMMIT must match the full immutable commit in the model lock")
SCHEMA_PATH = REPO_ROOT / "schemas" / "surge-projector-output-v1.0.schema.json"
SCHEMA = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))

SYSTEM = """あなたはSURGEの外部認識Projectorです。入力に明示された情報だけを抽出し、真偽や常識を補完しません。
出力は指定されたCanonical JSON Schemaに適合するJSONオブジェクト1個だけです。Markdown、コードフェンス、説明文は禁止です。
attributesは入力から直接支持される属性だけを格納します。keyは属性名、valueは原文に沿った値、polarityはpositive/negative/uncertain/unknown、categoryは意味カテゴリです。
relationsは原文が明示する二項関係だけを格納します。typeは関係名、sourceとtargetは関係の両端です。関係を推測で補いません。
evidence_spansは各属性・関係を直接支持する最短の連続原文範囲です。sourceはtextまたはcontext、startは0始まりUnicodeコードポイント位置、endは排他的、quoteは原文[source][start:end]と完全一致させます。1つの属性に複数根拠が必要なら複数spanを付けます。
具体例: textが「期限は水曜日です。」なら、期限属性はkey「期限」、value「水曜日」、polarity「positive」、category「temporal_update」、spanはsource「text」、start 3、end 6、quote「水曜日」です。
具体例: textが「期限は水曜日ではありません。」なら、期限属性のpolarityは「negative」です。「水曜日」という値だけを見てpositiveにしてはいけません。
具体例: textが「期限は水曜日です。AはBに提出する。」なら、属性deadlineのspanはsource「text」、start 3、end 6、quote「水曜日」です。関係のspanはsource「text」、start 9、end 18、quote「AはBに提出する。」です。関係はtype「提出先」、source「A」、target「B」です。出力形の一部は次のとおりです: {"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"deadline","value":"水曜日","polarity":"positive","category":"temporal_update","evidence_spans":[{"source":"text","start":3,"end":6,"quote":"水曜日"}]}],"relations":[{"type":"提出先","source":"A","target":"B","evidence_spans":[{"source":"text","start":9,"end":18,"quote":"AはBに提出する。"}]}]}
空配列と判断保留を区別します。該当情報が入力に存在しない場合、その配列は空にします。情報は存在するが指示対象・値・極性・関係が曖昧、矛盾、または確定不能な場合は、推測で属性や関係を作らずabstentionsにreasonを記録します。単に情報がないことをabstentionにしません。
Evidenceが取れない属性・関係は出力しません。空配列は許容されます。"""

PROMPT_TEMPLATE = (
    "<|im_start|>system\n{system}<|im_end|>\n"
    "<|im_start|>user\n"
    "Context原文: {context}\n"
    "Input原文: {text}\n"
    "上記原文のみを根拠にCanonical JSONを返してください。"
    "属性・関係を捏造せず、Evidence Spanのoffsetとquoteを照合してください。"
    "情報なしは空配列、情報はあるが判断不能はabstentionsです。"
    "{mode_hint}<|im_end|>\n<|im_start|>assistant\n"
)

def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def extract_json(text):
    """Accept exactly one JSON object; never scan banners/prompts for a stray brace."""
    if not isinstance(text, str):
        return None
    candidate = text.strip()
    if not candidate:
        return None
    try:
        value = json.loads(candidate)
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None

def run(model, model_path, case):
    prompt = PROMPT_TEMPLATE.format(
        system=SYSTEM, context=case.get("context", ""), text=case.get("text", ""),
        mode_hint="/no_think" if model.get("format") == "chatml_no_think" else ""
    )
    cmd = [
        "/usr/bin/time", "-v", CLI, "-m", str(model_path), "-p", prompt,
        "-n", "768", "--temp", "0", "--top-k", "1", "--threads", str(THREADS),
        "--no-display-prompt", "--simple-io", "--single-turn",
        "--no-show-timings", "--log-disable", "--json-schema-file", str(SCHEMA_PATH)
    ]
    started = time.perf_counter()
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, encoding="utf-8",
            errors="replace", timeout=240
        )
    except subprocess.TimeoutExpired:
        return {"case_id": case["case_id"], "timeout": True, "elapsed_seconds": 240}
    elapsed = time.perf_counter() - started
    parsed = extract_json(proc.stdout)
    rss = re.findall(r"Maximum resident set size \(kbytes\):\s*(\d+)", proc.stderr)
    result = {
        "case_id": case["case_id"], "return_code": proc.returncode,
        "elapsed_seconds": elapsed, "peak_rss_kib": int(rss[-1]) if rss else None,
        "json_valid": parsed is not None, "parsed_output": parsed,
        "raw_output": proc.stdout[:4000], "stderr": proc.stderr[-1000:],
        "decoding_constraint": "canonical-json-schema-file"
    }
    if parsed is not None:
        schema_errors = validate_projector_output(parsed, SCHEMA_PATH)
        result["projector_schema_valid"] = not schema_errors
        result["projector_schema_errors"] = schema_errors
        result["engineering_semantic_metrics"] = evaluate_case(case, parsed)
    else:
        result["projector_schema_valid"] = False
        result["projector_schema_errors"] = [{"path": "/", "message": "stdout is not exactly one JSON object"}]
    return result

def main():
    if not MODELS:
        raise SystemExit("model lock has no enabled candidates")
    if not shutil.which(CLI):
        raise SystemExit(f"llama-cli missing: {CLI}")
    corpus_path = Path(os.environ.get("SURGE_GOLD_CORPUS", "tests/fixtures/semantic/gold_seed_v0.1.json"))
    if not corpus_path.is_absolute():
        corpus_path = REPO_ROOT / corpus_path
    corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
    ROOT.mkdir(parents=True, exist_ok=True)
    cli_version = subprocess.run([CLI, "--version"], capture_output=True, text=True, timeout=30)
    if cli_version.returncode != 0:
        raise SystemExit("llama-cli --version failed")
    corpus_digest = sha256_file(corpus_path)
    profile_digest = hashlib.sha256(json.dumps(corpus.get("profile", {}), sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    report = {
        "matrix": "surge-structured-projector-matrix-v1.1",
        "corpus_id": corpus.get("corpus_id"),
        "corpus_status": "SEED_NOT_INDEPENDENT",
        "qualification_gate": "NOT_RUN",
        "expected_model_count": len(MODELS),
        "expected_case_ids": [case["case_id"] for case in corpus.get("cases", [])],
        "corpus_sha256": corpus_digest, "profile_sha256": profile_digest,
        "excluded_models": EXCLUDED_MODELS,
        "experiment_manifest": {
            "manifest_version": "surge-experiment-manifest-v1.1",
            "source_commit": os.environ.get("GITHUB_SHA", "unknown"),
            "llama_cpp_repository": LOCK["llama_cpp"]["repository"],
            "llama_cpp_commit": LLAMA_CPP_COMMIT,
            "model_lock_sha256": sha256_file(LOCK_PATH),
            "canonical_schema_sha256": sha256_file(SCHEMA_PATH),
            "llama_cli_path": str(Path(CLI).resolve()),
            "runtime": {
                "python": sys.version, "platform": platform.platform(),
                "machine": platform.machine(), "processor": platform.processor(),
                "huggingface_hub": __import__("huggingface_hub").__version__,
                "llama_cli_version": (cli_version.stdout or cli_version.stderr).strip()
            },
            "threads": THREADS,
            "generation": {
                "max_tokens": 768, "temperature": 0, "top_k": 1,
                "timeout_seconds": 240, "constrained_decoding": "canonical-json-schema-file"
            },
            "system_prompt_sha256": hashlib.sha256(SYSTEM.encode()).hexdigest(),
            "prompt_template_sha256": hashlib.sha256(PROMPT_TEMPLATE.encode()).hexdigest(),
            "corpus_sha256": corpus_digest, "profile_sha256": profile_digest,
            "models": [{
                "repository": m["repo"], "revision": m["revision"],
                "tokenizer_repository": m["tokenizer_repo"],
                "tokenizer_revision": m["tokenizer_revision"], "file": m["file"]
            } for m in MODELS]
        },
        "models": []
    }
    for model in MODELS:
        destination = ROOT / model["name"]
        destination.mkdir(parents=True, exist_ok=True)
        model_path = Path(hf_hub_download(
            repo_id=model["repo"], filename=model["file"],
            revision=model["revision"], local_dir=str(destination)
        ))
        model_report = {
            "name": model["name"], "repository": model["repo"],
            "repository_revision": model["revision"],
            "tokenizer_repository": model["tokenizer_repo"],
            "tokenizer_revision": model["tokenizer_revision"],
            "tokenizer_source": "embedded GGUF tokenizer metadata; same immutable repository revision",
            "model_file": model["file"], "file_bytes": model_path.stat().st_size,
            "file_sha256": sha256_file(model_path), "results": []
        }
        for case in corpus["cases"]:
            row = run(model, model_path, case)
            if row.get("return_code", 0) != 0 and not row.get("timeout"):
                raise RuntimeError(f"model command failed: {model['name']} {case['case_id']}")
            model_report["results"].append(row)
        rows = model_report["results"]
        elapsed = [row["elapsed_seconds"] for row in rows if "elapsed_seconds" in row]
        rss = [row["peak_rss_kib"] for row in rows if row.get("peak_rss_kib") is not None]
        model_report.update({
            "json_valid_cases": sum(bool(row.get("json_valid")) for row in rows),
            "schema_valid_cases": sum(bool(row.get("projector_schema_valid")) for row in rows),
            "coverage": sum(bool(row.get("engineering_semantic_metrics")) for row in rows) / len(rows) if rows else 0.0,
            "timeouts": sum(bool(row.get("timeout")) for row in rows),
            "latency_mean_seconds": sum(elapsed) / len(elapsed) if elapsed else None,
            "latency_p50_seconds": median(elapsed) if elapsed else None,
            "latency_max_seconds": max(elapsed) if elapsed else None,
            "peak_rss_kib_max": max(rss) if rss else None
        })
        report["models"].append(model_report)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
