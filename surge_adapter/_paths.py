"""Runtime resource paths for source and installed SURGE packages."""
from __future__ import annotations

import os
import sys
from pathlib import Path


def schema_path(filename: str) -> Path:
    if not filename or Path(filename).name != filename:
        raise ValueError("SCHEMA_FILENAME_INVALID")
    source_tree = Path(__file__).resolve().parents[1] / "schemas" / filename
    if source_tree.is_file():
        return source_tree
    installed = Path(sys.prefix) / "share" / "surge" / "schemas" / filename
    if installed.is_file():
        return installed
    raise FileNotFoundError(f"SURGE_SCHEMA_NOT_INSTALLED:{filename}")


def native_core_candidates(explicit: str | Path | None = None) -> list[Path]:
    """Return deterministic native Core candidates in precedence order.

    SURGE_CORE_LIBRARY is the strongest override. SURGE_CORE_PREFIX and the
    active Python prefix support relocatable CMake installs. Source build
    locations remain a development fallback.
    """
    if sys.platform == "win32":
        names = ("surge_core.dll",)
    elif sys.platform == "darwin":
        names = ("libsurge_core.dylib",)
    else:
        names = ("libsurge_core.so",)
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(Path(explicit).expanduser())
    configured = os.environ.get("SURGE_CORE_LIBRARY")
    if configured:
        candidates.append(Path(configured).expanduser())

    prefixes: list[Path] = []
    configured_prefix = os.environ.get("SURGE_CORE_PREFIX")
    if configured_prefix:
        prefixes.append(Path(configured_prefix).expanduser())
    prefixes.extend((Path(sys.prefix), Path(sys.base_prefix)))
    for prefix in prefixes:
        for directory in (prefix / "lib", prefix / "lib64", prefix / "bin", prefix):
            candidates.extend(directory / name for name in names)
            if sys.platform != "win32" and sys.platform != "darwin":
                candidates.extend(sorted(directory.glob("libsurge_core.so.*")) if directory.is_dir() else [])

    root = Path(__file__).resolve().parents[1]
    for relative in (
        "build/libsurge_core.so",
        "build/libsurge_core.dylib",
        "build/surge_core.dll",
        "build/Release/surge_core.dll",
    ):
        candidates.append(root / relative)

    unique: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = os.path.normcase(str(candidate))
        if key not in seen:
            seen.add(key)
            unique.append(candidate)
    return unique


def resolve_native_core_library(explicit: str | Path | None = None) -> Path:
    """Resolve the first existing Core library using documented precedence."""
    for candidate in native_core_candidates(explicit):
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "SURGE C++ shared library not found; install it with CMake, set "
        "SURGE_CORE_PREFIX or SURGE_CORE_LIBRARY, or build the source tree"
    )
