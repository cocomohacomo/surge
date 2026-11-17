#!/usr/bin/env python3
"""Run a pluggable meeting-decision adapter over a frozen JSONL case set."""
from __future__ import annotations
import argparse
import importlib
import json
import pathlib
import sys
import unicodedata


def load_rows(path: pathlib.Path):
    rows = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.strip():
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError(f"{path}:{line_no}: RECORD_NOT_OBJECT")
            rows.append(value)
    return rows


def load_predictor(spec: str):
    if ":" not in spec:
        raise ValueError("ADAPTER_MUST_BE_MODULE_COLON_FUNCTION")
    module_name, function_name = spec.split(":", 1)
    function = getattr(importlib.import_module(module_name), function_name)
    if not callable(function):
        raise ValueError("ADAPTER_ENTRYPOINT_NOT_CALLABLE")
    return function


def run(cases, predictor):
    seen = set()
    outputs = []
    for case in cases:
        case_id = case.get("case_id")
        source = case.get("source_text")
        digest = case.get("source_sha256")
        if not isinstance(case_id, str) or not case_id or case_id in seen:
            raise ValueError("CASE_ID_MISSING_OR_DUPLICATE")
        seen.add(case_id)
        if not isinstance(source, str) or not isinstance(digest, str):
            raise ValueError(f"{case_id}: CASE_SOURCE_INVALID")
        result = predictor(case)
        if not isinstance(result, dict) or set(result) != {"decision", "items"}:
            raise ValueError(f"{case_id}: ADAPTER_RESULT_SHAPE_INVALID")
        if result["decision"] not in ("ACCEPT", "REVIEW", "ABSTAIN") or not isinstance(result["items"], list):
            raise ValueError(f"{case_id}: ADAPTER_RESULT_VALUE_INVALID")
        outputs.append({"case_id": case_id, "source_sha256": digest,
                        "decision": result["decision"], "items": result["items"]})
    return outputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", required=True, type=pathlib.Path)
    parser.add_argument("--adapter", required=True, help="Python module:function exposing predict(case)")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    predictor = load_predictor(args.adapter)
    results = run(load_rows(args.cases), predictor)
    args.output.write_text("".join(json.dumps(x, ensure_ascii=False) + "\n" for x in results),
                           encoding="utf-8")
    print(f"adapter records: {len(results)} -> {args.output}")


if __name__ == "__main__":
    main()
