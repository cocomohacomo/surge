#!/usr/bin/env python3
"""CPU-only SURGE projector model smoke benchmark.

Downloads pinned GGUF files, runs identical Japanese recognition prompts through
llama.cpp, and emits a machine-readable report. This is a model capability smoke
test, not a semantic-quality certification.
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

from huggingface_hub import hf_hub_download

ROOT = Path(os.environ.get("SURGE_MODEL_BENCH_DIR", "model-bench"))
REPORT = Path(os.environ.get("SURGE_MODEL_BENCH_REPORT", ROOT / "report.json"))
LLAMA_CLI = os.environ.get("LLAMA_CLI", "llama-cli")
THREADS = max(1, int(os.environ.get("SURGE_MODEL_THREADS", str(os.cpu_count() or 2))))

MODELS = [
    {
        "name": "qwen3-0.6b-q4_k_m",
        "repo": "gvij/qwen3-0.6b-gguf",
        "file": "qwen3-0.6b-q4_k_m.gguf",
        "format": "qwen3-chatml-no-think",
    },
    {
        "name": "smollm2-135m-instruct-q4_k_m",
        "repo": "bartowski/SmolLM2-135M-Instruct-GGUF",
        "file": "SmolLM2-135M-Instruct-Q4_K_M.gguf",
        "format": "chatml",
    },
]

CASES = [
    {
        "id": "short_ack_context",
        "context": "会議で担当者が『明日までに提出できますか』と質問した。",
        "input": "うん",
        "must_contain": ["うん"],
        "task": "短い応答を文脈付きで候補化する。断定しすぎない。",
    },
    {
        "id": "contradiction",
        "context": "同じ会議について、前半では『締切は火曜日』、後半では『締切は水曜日』と発言された。",
        "input": "締切は水曜日です。",
        "must_contain": ["水曜日"],
        "task": "既存候補と新規候補を両方保持し、矛盾候補の関係を出す。",
    },
    {
        "id": "exception_and_scope",
        "context": "原則は申請期限を30日とする。ただし災害時はこの限りではない。",
        "input": "災害時は申請期限の例外とする。",
        "must_contain": ["災害", "例外"],
        "task": "一般規則と例外の適用範囲を分離する。",
    },
    {
        "id": "effective_date",
        "context": "旧制度は4月1日から適用。改正制度は翌年4月1日から適用。",
        "input": "改正制度の適用開始日は2027年4月1日です。",
        "must_contain": ["2027年4月1日"],
        "task": "制度と適用時期を混同せず候補化する。",
    },
    {
        "id": "cross_block_reference",
        "context": "第1ブロック: A地区の工事費は120万円。第2ブロック: この費用は補助対象外とする。",
        "input": "この費用は補助対象外です。",
        "must_contain": ["補助対象外"],
        "task": "指示語の参照先候補を明示し、根拠が不足する場合は不確実性を残す。",
    },
]

SYSTEM = (
    "あなたはSURGEの外部Projectorです。真偽や採否を決定せず、"
    "入力から認知候補を抽出してください。必ずJSONオブジェクトだけを返してください。"
    "形式: {\"candidates\":[{\"text\":\"...\",\"kind\":\"claim|question|uncertain\","
    "\"confidence\":0.0,\"evidence_quote\":\"...\"}],"
    "\"relations\":[{\"type\":\"supports|contradicts|refers_to|exception_of\","
    "\"source\":\"...\",\"target\":\"...\"}],"
    "\"uncertainties\":[\"...\"]}。"
    "confidenceは候補生成上の目安であり真偽確率ではありません。"
)

def make_prompt(model_format: str, case: dict) -> str:
    user = (
        f"タスク: {case['task']}\n"
        f"既存文脈: {case['context']}\n"
        f"新規入力: {case['input']}\n"
        "JSONを返してください。"
    )
    if model_format.startswith("qwen3"):
        return (
            f"<|im_start|>system\n{SYSTEM}<|im_end|>\n"
            f"<|im_start|>user\n{user}<|im_end|>\n"
            "<|im_start|>assistant\n"
        )
    return (
        f"<|im_start|>system\n{SYSTEM}<|im_end|>\n"
        f"<|im_start|>user\n{user}<|im_end|>\n"
        "<|im_start|>assistant\n"
    )

def extract_json(text: str):
    decoder = json.JSONDecoder()
    for index, char in enumerate(text):
        if char == "{":
            try:
                value, _ = decoder.raw_decode(text[index:])
                if isinstance(value, dict):
                    return value
            except json.JSONDecodeError:
                continue
    return None

def run_case(model_path: Path, model: dict, case: dict) -> dict:
    prompt = make_prompt(model["format"], case)
    command = [
        LLAMA_CLI, "-m", str(model_path), "-p", prompt,
        "-n", "192", "--temp", "0", "--top-k", "1",
        "--threads", str(THREADS), "--no-display-prompt", "--simple-io",
    ]
    start = time.perf_counter()
    proc = subprocess.run(command, capture_output=True, text=True, timeout=240)
    elapsed = time.perf_counter() - start
    output = proc.stdout.strip()
    parsed = extract_json(output)
    candidates = parsed.get("candidates", []) if parsed else []
    relations = parsed.get("relations", []) if parsed else []
    uncertainties = parsed.get("uncertainties", []) if parsed else []
    combined = json.dumps(parsed, ensure_ascii=False) if parsed else output
    expected_hits = sum(token in combined for token in case["must_contain"])
    perf_text = proc.stderr + "\n" + proc.stdout
    speed_matches = re.findall(r"([0-9]+(?:\\.[0-9]+)?)\\s*tokens per second", perf_text)
    rss_matches = re.findall(r"Maximum resident set size \\(kbytes\\):\\s*(\\d+)", perf_text)
    return {
        "case_id": case["id"],
        "return_code": proc.returncode,
        "elapsed_seconds": round(elapsed, 4),
        "tokens_per_second": float(speed_matches[-1]) if speed_matches else None,
        "peak_rss_kib": int(rss_matches[-1]) if rss_matches else None,
        "json_valid": parsed is not None,
        "candidate_count": len(candidates) if isinstance(candidates, list) else None,
        "relation_count": len(relations) if isinstance(relations, list) else None,
        "uncertainty_count": len(uncertainties) if isinstance(uncertainties, list) else None,
        "expected_marker_hits": expected_hits,
        "expected_marker_total": len(case["must_contain"]),
        "output_excerpt": output[:1600],
        "stderr_excerpt": proc.stderr[-1200:],
    }

def main() -> int:
    if not shutil.which(LLAMA_CLI):
        raise SystemExit(f"llama-cli not found: {LLAMA_CLI}")
    ROOT.mkdir(parents=True, exist_ok=True)
    report = {
        "benchmark": "surge-japanese-projector-cpu-smoke-v0.1",
        "runtime": "llama.cpp CPU",
        "threads": THREADS,
        "cases": len(CASES),
        "models": [],
        "limitations": [
            "GitHub-hosted CPU runner measurements are comparative, not target-device benchmarks.",
            "Five synthetic cases are a smoke test, not a statistically valid Japanese quality evaluation.",
            "Model output is untrusted projector input; SURGE Core remains responsible only for structural validation and persistence.",
        ],
    }
    for model in MODELS:
        model_dir = ROOT / model["name"]
        model_dir.mkdir(parents=True, exist_ok=True)
        model_path = Path(hf_hub_download(
            repo_id=model["repo"], filename=model["file"], local_dir=str(model_dir)
        ))
        model_result = {"name": model["name"], "repo": model["repo"], "file": model["file"], "file_bytes": model_path.stat().st_size, "results": []}
        for case in CASES:
            try:
                result = run_case(model_path, model, case)
            except subprocess.TimeoutExpired:
                result = {"case_id": case["id"], "timeout": True}
            model_result["results"].append(result)
        valid = [r for r in model_result["results"] if r.get("json_valid")]
        model_result["json_valid_cases"] = len(valid)
        model_result["expected_marker_hits"] = sum(r.get("expected_marker_hits", 0) for r in model_result["results"])
        model_result["expected_marker_total"] = sum(r.get("expected_marker_total", 0) for r in model_result["results"])
        model_result["mean_elapsed_seconds"] = round(
            sum(r.get("elapsed_seconds", 0) for r in model_result["results"]) / len(CASES), 4
        )
        report["models"].append(model_result)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
