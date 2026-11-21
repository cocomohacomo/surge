"""Runtime paths for schemas shared by source and wheel installations."""
from __future__ import annotations

from pathlib import Path
import sys


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
