#!/usr/bin/env python3
"""Verify all manifest C ABI functions exist in the built shared library."""
from __future__ import annotations
import argparse
import ctypes
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def parse_symbols(output: str, platform: str) -> set[str]:
    symbols: set[str] = set()
    for line in output.splitlines():
        line = line.strip()
        if not line:
            continue
        if platform == "win32":
            match = re.match(r"^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$", line)
            if match:
                symbols.add(match.group(1).split("=")[0])
        else:
            parts = line.split()
            if not parts:
                continue
            symbol = parts[-1]
            if symbol.startswith("_"):
                symbol = symbol[1:]
            if symbol.startswith("surge_"):
                symbols.add(symbol)
    return symbols


def read_symbols(library: Path, platform: str) -> set[str]:
    if platform == "win32":
        module = ctypes.WinDLL(str(library))
        manifest = json.loads(Path("abi/c-abi-v0.1.json").read_text())
        return {item["name"] for item in manifest["functions"]
                if getattr(module, item["name"], None) is not None}
    elif platform == "darwin":
        command = ["nm", "-gU", str(library)]
    else:
        command = ["nm", "-D", "--defined-only", str(library)]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    return parse_symbols(result.stdout, platform)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--manifest", type=Path, default=Path("abi/c-abi-v0.1.json"))
    parser.add_argument("--platform", default=sys.platform)
    args = parser.parse_args()
    if not args.library.is_file():
        raise SystemExit("ABI_LIBRARY_NOT_FOUND:" + str(args.library))
    expected = {item["name"] for item in json.loads(args.manifest.read_text())["functions"]}
    actual = read_symbols(args.library, args.platform)
    missing = sorted(expected - actual)
    if missing:
        print("ABI_BINARY_EXPORTS_MISSING:" + ",".join(missing))
        return 1
    print(f"C ABI binary exports OK: {len(expected)} manifest functions present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
