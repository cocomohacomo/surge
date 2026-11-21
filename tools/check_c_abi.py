#!/usr/bin/env python3
"""Check reviewed public C ABI declarations against a checked-in baseline.

Source-level guard for function signatures and public struct field order/types.
It does not replace platform binary export inspection or compiler-specific
sizeof/offsetof checks.
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
    value = re.sub(r"\s+", " ", value).strip()
    return re.sub(r"\s*([(),*])\s*", r"\1", value)

def extract_surface(source: str) -> dict:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"//[^\n]*", "", source)
    versions = {}
    for part in ("MAJOR", "MINOR", "PATCH"):
        found = re.search(r"#define\s+SURGE_ABI_VERSION_" + part + r"\s+(\d+)", source)
        if not found:
            raise SystemExit("ABI_VERSION_MACRO_MISSING:" + part)
        versions[part.lower()] = int(found.group(1))
    source = re.sub(r"(?m)^\s*#.*$", "", source)
    source = source.replace('extern "C" {', "")
    structs = {}
    for match in re.finditer(
        r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", source, flags=re.S
    ):
        tag, body, alias = match.groups()
        structs[alias] = {
            "tag": tag,
            "fields": [normalize(item) for item in body.split(";") if item.strip()],
        }
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
            match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*$", statement, re.S)
            if not match or not match.group(1).startswith("surge_"):
                continue
            declarations.append({
                "name": match.group(1),
                "return_type": normalize(statement[:match.start()]),
                "parameters": normalize(match.group(2)),
            })
    declarations.sort(key=lambda item: item["name"])
    if len({item["name"] for item in declarations}) != len(declarations):
        raise SystemExit("ABI_MANIFEST_DUPLICATE_FUNCTION")
    return {
        "abi_version": versions,
        "structs": dict(sorted(structs.items())),
        "functions": declarations,
    }

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    current = extract_surface(HEADER.read_text(encoding="utf-8"))
    if args.write:
        MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        MANIFEST.write_text(json.dumps(current, indent=2) + "\n", encoding="utf-8")
        print("wrote " + str(MANIFEST.relative_to(ROOT)))
        return 0
    expected = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if current != expected:
        print("C_ABI_SURFACE_CHANGED: review ABI version and update abi/c-abi-v0.1.json intentionally")
        for section in ("abi_version", "structs", "functions"):
            if current[section] == expected.get(section):
                continue
            print("DIFF_SECTION:" + section)
            if section == "functions":
                old_items = {item["name"]: item for item in expected.get(section, [])}
                new_items = {item["name"]: item for item in current[section]}
                print("MISSING_FUNCTIONS:" + ",".join(sorted(old_items.keys() - new_items.keys())))
                print("NEW_FUNCTIONS:" + ",".join(sorted(new_items.keys() - old_items.keys())))
                for name in sorted(old_items.keys() & new_items.keys()):
                    if old_items[name] != new_items[name]:
                        print("CHANGED_FUNCTION:" + name)
                        print("EXPECTED:" + json.dumps(old_items[name], sort_keys=True))
                        print("ACTUAL:" + json.dumps(new_items[name], sort_keys=True))
                        break
            elif section == "structs":
                print("EXPECTED_STRUCTS:" + ",".join(expected.get(section, {}).keys()))
                print("ACTUAL_STRUCTS:" + ",".join(current[section].keys()))
                for name in sorted(current[section].keys() & expected.get(section, {}).keys()):
                    if current[section][name] != expected[section][name]:
                        print("CHANGED_STRUCT:" + name)
                        print("EXPECTED:" + json.dumps(expected[section][name], sort_keys=True))
                        print("ACTUAL:" + json.dumps(current[section][name], sort_keys=True))
                        break
            else:
                print("EXPECTED:" + json.dumps(expected.get(section), sort_keys=True))
                print("ACTUAL:" + json.dumps(current[section], sort_keys=True))
        return 1
    print(f"C ABI declaration surface OK: {len(current['functions'])} functions, {len(current['structs'])} structs")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

