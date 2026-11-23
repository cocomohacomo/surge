#!/usr/bin/env python3
"""Reject source/build-tree paths leaking into an installed CMake package."""
from __future__ import annotations
import sys
from pathlib import Path


def normalized(path: str) -> str:
    return str(Path(path).resolve()).replace("\\", "/").rstrip("/").casefold()


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: check_cmake_install_tree.py PREFIX SOURCE_ROOT BUILD_ROOT")
    prefix, source_root, build_root = map(normalized, sys.argv[1:])
    config_dir = Path(sys.argv[1]).resolve() / "lib" / "cmake" / "Surge"
    if not config_dir.is_dir():
        # Windows and multiarch layouts can place package configs elsewhere.
        matches = list(Path(sys.argv[1]).resolve().rglob("SurgeTargets.cmake"))
        if not matches:
            raise SystemExit("INSTALLED_SURGE_CMAKE_PACKAGE_NOT_FOUND")
        config_files = [matches[0], matches[0].with_name("SurgeConfig.cmake")]
    else:
        config_files = sorted(config_dir.glob("*.cmake"))
    checked = 0
    for path in config_files:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace").replace("\\", "/").casefold()
        for forbidden, label in ((source_root, "SOURCE"), (build_root, "BUILD")):
            if forbidden and forbidden in text:
                raise SystemExit(f"INSTALL_TREE_{label}_PATH_LEAK:{path.name}")
        checked += 1
    if checked == 0:
        raise SystemExit("INSTALLED_SURGE_CMAKE_CONFIG_EMPTY")
    print(f"installed CMake package path inspection passed: {checked} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
