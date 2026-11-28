#!/usr/bin/env python3
"""CPU-only benchmark for the SURGE minimal information projector.

The model selects one candidate cluster (or null) and emits one opaque numeric
score. It does not extract attributes, relations, phenomena, or evidence spans.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(os.environ.get("SURGE_MODEL_BENCH_DIR", "model-bench"))
REPORT = Path(os.environ.get("SURGE_MODEL_BENCH_REPORT", ROOT / "minimal-projector-report.json"))
LLAMA_CLI = os.environ.get("LLAMA_CLI", "llama-cli")
THREADS = max(1, int(os.environ.get("SURGE_MODEL_THREADS", "2")))
MAX_TOKENS = 64

MODELS = [
    {"name": "qwen3-0.6b-q4_k_m", "repo": "gvij/qwen3-0.6b-gguf",
     "file": "qwen3-0.6b-q4_k_m.gguf", "format": "qwen3"},
    {"name": "smollm2-135m-instruct-q4_k_m", "repo": "bartowski/SmolLM2-135M-Instruct-GGUF",
     "file": "SmolLM2-135M-Instruct-Q4_K_M.gguf", "format": "chatml"},
]

CASES = [
    {"id": "shiba_to_dog", "information": "柴犬",
     "clusters": [{"id": "cluster-dog", "description": "犬に関する情報"},
                  {"id": "cluster-cat", "description": "猫に関する情報"}],
     "expected_target": "cluster-dog"},
    {"id": "cat_to_cat", "information": "三毛猫",
     "clusters": [{"id": "cluster-dog", "description": "犬に関する情報"},
                  {"id": "cluster-cat", "description": "猫に関する情報"}],
     "expected_target": "cluster-cat"},
    {"id": "meeting_deadline", "information": "締切は水曜日です。",
     "clusters": [{"id": "cluster-meeting", "description": "会議の進行と議題"},
                  {"id": "cluster-deadline", "description": "提出期限と日程"}],
     "expected_target": "cluster-deadline"},
    {"id": "disaster_exception", "information": "災害時は申請期限の例外とする。",
     "clusters": [{"id": "cluster-application", "description": "申請手続と期限"},
                  {"id": "cluster-weather", "description": "天候と季節"}],
     "expected_target": "cluster-application"},
    {"id": "no_candidate", "information": "青い彗星が南の空に見えた。",
     "clusters": [{"id": "cluster-dog", "description": "犬に関する情報"},
                  {"id": "cluster-budget", "description": "予算と会計"}],
     "expected_target": None},
]

SYSTEM = (
    "あなたはSURGEの外部Minimal Projectorです。新規情報を既存クラスター候補と比較し、"
    "最も近い候補を1つ選んでください。十分に近い候補がなければtarget_clusterはnullです。"
    "情報の意味属性、関係、根拠位置、説明文は生成しません。"
    "出力はJSONオブジェクト1個のみ。キーはinformation,target_cluster,distanceです。"
    "informationは入力をそのまま複写。target_clusterは候補IDまたはnull。"
    "distanceはこのベンチマーク内の近さスコアで0から100の整数、100が最も近く、"
    "候補なしは0。余分なキーは禁止。"
)

def make_prompt(model_format: str, case: dict) -> str:
    payload = {
        "information": case["information"],
        "candidate_clusters": case["clusters"],
    }
    user = "次の情報を候補クラスターへ投射してください。\n" + json.dumps(payload, ensure_ascii=False)
    return (
        f"<|im_start|>system\n{SYSTEM}<|im_end|>\n"
        f"<|im_start|>user\n{user}<|im_end|>\n"
        "<|im_start|>assistant\n"
    )

def extract_json(output: str):
    if not isinstance(output, str):
        return None
    candidate = output.strip()
    try:
        value = json.loads(candidate)
    except json.JSONDecodeError:
        marker = " [end of text]"
        if not candidate.endswith(marker):
            return None
        try:
            value = json.loads(candidate[:-len(marker)].rstrip())
        except json.JSONDecodeError:
            return None
    return value if isinstance(value, dict) else None

def validate_projection(value, information: str, candidate_ids: set[str]) -> bool:
    if not isinstance(value, dict) or set(value) != {"information", "target_cluster", "distance"}:
        return False
    if value["information"] != information:
        return False
    target = value["target_cluster"]
    if target is not None and (not isinstance(target, str) or target not in candidate_ids):
        return False
    distance = value["distance"]
    return isinstance(distance, int) and not isinstance(distance, bool) and 0 <= distance <= 100

def run_case(model_path: Path, model: dict, case: dict) -> dict:
    prompt = make_prompt(model["format"], case)
    command = [
        "/usr/bin/time", "-v", LLAMA_CLI, "-m", str(model_path), "-p", prompt,
        "-n", str(MAX_TOKENS), "--temp", "0", "--top-k", "1",
        "--threads", str(THREADS), "--no-display-prompt", "--simple-io",
    ]
    started = time.perf_counter()
    proc = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=180)
    elapsed = time.perf_counter() - started
    parsed = extract_json(proc.stdout)
    ids = {item["id"] for item in case["clusters"]}
    valid = validate_projection(parsed, case["information"], ids)
    speed = re.findall(r"([0-9]+(?:\.[0-9]+)?)\s*tokens per second", proc.stderr + proc.stdout)
    rss = re.findall(r"Maximum resident set size \(kbytes\):\s*(\d+)", proc.stderr)
    actual = parsed.get("target_cluster") if valid else None
    return {
        "case_id": case["id"], "return_code": proc.returncode,
        "elapsed_seconds": round(elapsed, 4),
        "tokens_per_second": float(speed[-1]) if speed else None,
        "peak_rss_kib": int(rss[-1]) if rss else None,
        "json_valid": parsed is not None, "projection_valid": valid,
        "expected_target": case["expected_target"], "actual_target": actual,
        "target_correct": valid and actual == case["expected_target"],
        "output": parsed, "output_excerpt": proc.stdout[-1000:],
        "stderr_excerpt": proc.stderr[-1000:],
    }

def main() -> int:
    from huggingface_hub import hf_hub_download
    if not shutil.which(LLAMA_CLI):
        raise SystemExit(f"llama-cli not found: {LLAMA_CLI}")
    ROOT.mkdir(parents=True, exist_ok=True)
    report = {
        "benchmark": "surge-minimal-projector-cpu-v0.2",
        "runtime": "llama.cpp CPU", "threads": THREADS,
        "max_generation_tokens": MAX_TOKENS,
        "output_contract": ["information", "target_cluster", "distance"],
        "distance_profile": "benchmark_similarity_0_100_only",
        "models": [],
        "limitations": [
            "Synthetic five-case smoke test; not a Japanese production-quality certification.",
            "The benchmark score profile is local to this benchmark; Core treats distance as opaque.",
            "GitHub runner RSS and latency are not target-device measurements.",
            "SmolLM2 is primarily English-oriented and serves as a footprint baseline.",
        ],
    }
    for model in MODELS:
        model_dir = ROOT / model["name"]
        model_dir.mkdir(parents=True, exist_ok=True)
        model_path = Path(hf_hub_download(repo_id=model["repo"], filename=model["file"],
                                          local_dir=str(model_dir)))
        results = []
        for case in CASES:
            try:
                result = run_case(model_path, model, case)
            except subprocess.TimeoutExpired:
                result = {"case_id": case["id"], "timeout": True,
                          "projection_valid": False, "target_correct": False}
            if result.get("return_code", 0) != 0:
                raise RuntimeError(f"{model['name']} failed on {case['id']}: {result.get('stderr_excerpt', '')}")
            results.append(result)
        valid = sum(bool(item.get("projection_valid")) for item in results)
        correct = sum(bool(item.get("target_correct")) for item in results)
        model_report = {
            "name": model["name"], "repo": model["repo"], "file": model["file"],
            "file_bytes": model_path.stat().st_size, "results": results,
            "valid_projection_cases": valid, "correct_target_cases": correct,
            "target_accuracy": round(correct / len(CASES), 4),
            "mean_elapsed_seconds": round(
                sum(item.get("elapsed_seconds", 0) for item in results) / len(CASES), 4),
            "max_peak_rss_kib": max(
                (item.get("peak_rss_kib", 0) for item in results), default=0),
        }
        report["models"].append(model_report)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
