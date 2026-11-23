#!/usr/bin/env python3
"""Enforce version-aware compatibility against immutable public ABI baselines."""
from __future__ import annotations
import json
from pathlib import Path

from check_c_abi import extract_surface, HEADER, ROOT


BASELINES = ROOT / "abi" / "compatibility-baselines"


def version_tuple(surface: dict) -> tuple[int, int, int]:
    value = surface["abi_version"]
    return value["major"], value["minor"], value["patch"]


def compare(current: dict, baseline: dict, label: str) -> list[str]:
    errors: list[str] = []
    current_version = version_tuple(current)
    baseline_version = version_tuple(baseline)
    if current_version < baseline_version:
        return [f"{label}:ABI_VERSION_REGRESSION"]
    if current_version[0] > baseline_version[0]:
        return errors
    if current_version[0] < baseline_version[0]:
        return [f"{label}:ABI_MAJOR_REGRESSION"]

    old_functions = {item["name"]: item for item in baseline["functions"]}
    new_functions = {item["name"]: item for item in current["functions"]}
    removed = sorted(old_functions.keys() - new_functions.keys())
    changed = sorted(name for name in old_functions.keys() & new_functions.keys()
                     if old_functions[name] != new_functions[name])
    if removed:
        errors.append(f"{label}:REMOVED_FUNCTIONS:" + ",".join(removed))
    if changed:
        errors.append(f"{label}:CHANGED_FUNCTION_SIGNATURES:" + ",".join(changed))

    old_structs, new_structs = baseline["structs"], current["structs"]
    removed_structs = sorted(old_structs.keys() - new_structs.keys())
    changed_structs = sorted(name for name in old_structs.keys() & new_structs.keys()
                             if old_structs[name] != new_structs[name])
    if removed_structs:
        errors.append(f"{label}:REMOVED_STRUCTS:" + ",".join(removed_structs))
    if changed_structs:
        errors.append(f"{label}:CHANGED_STRUCT_LAYOUTS:" + ",".join(changed_structs))

    additions = (new_functions.keys() - old_functions.keys()) | (new_structs.keys() - old_structs.keys())
    if additions and current_version[1] == baseline_version[1]:
        errors.append(f"{label}:ADDITIVE_ABI_CHANGE_REQUIRES_MINOR_BUMP")
    return errors


def main() -> int:
    current = extract_surface(HEADER.read_text(encoding="utf-8"))
    files = sorted(BASELINES.glob("c-abi-v*.json"))
    if not files:
        raise SystemExit("ABI_COMPATIBILITY_BASELINE_MISSING")
    errors = []
    checked = 0
    for path in files:
        baseline = json.loads(path.read_text(encoding="utf-8"))
        errors.extend(compare(current, baseline, path.name))
        checked += 1
    if errors:
        print("\n".join(errors))
        return 1
    print(f"C ABI compatibility OK against {checked} immutable baseline(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
