#!/usr/bin/env python3
"""Check the reviewed public C ABI declaration surface against its baseline.

This is a source-level compatibility guard. It detects declaration, parameter,
and public struct field-order/type changes. It does not replace platform binary
export inspection or compiler-specific sizeof/offsetof checks.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "surge" / "c_api.h"
MANIFEST = ROOT / "abi" / "c-abi-v0.1.json"


def normalize(value: str) -> str:
    value = re.sub(r"\\s+", " ", value).strip()
    value = re.sub(r"\\s*([(),*])\\s*", r"\\1", value)
    return value


def extract_surface(source: str) -> dict:
    source = re.sub(r"/\\*.*?\\*/", "", source, flags=re.S)
    source = re.sub(r"//[^\\n]*", "", source)
    structs = {}
    for match in re.finditer(
        r"typedef\\s+struct\\s+(\\w+)\\s*\\{(.*?)\\}\\s*(\\w+)\\s*;",
        source,
        flags=re.S,
    ):
        tag, body, alias = match.groups()
        fields = [normalize(item) for item in body.split(";") if item.strip()]
        structs[alias] = {"tag": tag, "fields": fields}

    declarations = []
    depth = 0
    start = 0
    for index, char in enumerate(source):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        elif char == ";" and depth == 0:
            statement = source[start:index].strip()
            start = index + 1
            if "surge_" not in statement or "(" not in statement:
                continue
            statement = statement.replace('extern "C"', "").strip()
            match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\\s*\\((.*)\\)\\s*$", statement, re.S)
            if not match:
                continue
            name, parameters = match.groups()
            if not name.startswith("surge_"):
                continue
            prefix = statement[:match.start()].strip()
            declarations.append({
                "name": name,
                "return_type": normalize(prefix),
                "parameters": normalize(parameters),
            })

    declarations.sort(key=lambda item: item["name"])
    if len({item["name"] for item in declarations}) != len(declarations):
        raise SystemExit("ABI_MANIFEST_DUPLICATE_FUNCTION")
    return {
        "abi_version": {
            "major": int(re.search(r"#define\\s+SURGE_ABI_VERSION_MAJOR\\s+(\\d+)", source).group(1)),
            "minor": int(re.search(r"#define\\s+SURGE_ABI_VERSION_MINOR\\s+(\\d+)", source).group(1)),
            "patch": int(re.search(r"#define\\s+SURGE_ABI_VERSION_PATCH\\s+(\\d+)", source).group(1)),
        },
        "structs": dict(sorted(structs.items())),
        "functions": declarations,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="write the reviewed manifest")
    args = parser.parse_args()
    current = extract_surface(HEADER.read_text(encoding="utf-8"))
    if args.write:
        MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        MANIFEST.write_text(json.dumps(current, indent=2, ensure_ascii=False) + "\\n", encoding="utf-8")
        print(f"wrote {MANIFEST.relative_to(ROOT)}")
        return 0
    expected = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if current != expected:
        print("C_ABI_SURFACE_CHANGED: review ABI version and update abi/c-abi-v0.1.json intentionally")
        return 1
    print(f"C ABI declaration surface OK: {len(current['functions'])} functions, {len(current['structs'])} structs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
