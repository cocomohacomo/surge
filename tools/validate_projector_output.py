#!/usr/bin/env python3
"""Validate canonical SURGE projector output independently from semantic scoring."""
import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker


def validate(payload, schema_path=None):
    schema_path = schema_path or (
        Path(__file__).resolve().parents[1]
        / "schemas" / "surge-projector-output-v1.0.schema.json"
    )
    schema = json.loads(Path(schema_path).read_text(encoding="utf-8"))
    validator = Draft202012Validator(schema, format_checker=FormatChecker())
    errors = sorted(validator.iter_errors(payload), key=lambda error: list(error.absolute_path))
    return [{
        "path": "/" + "/".join(str(part) for part in error.absolute_path),
        "message": error.message,
    } for error in errors]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: validate_projector_output.py OUTPUT.json")
    payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    errors = validate(payload)
    print(json.dumps({"schema_valid": not errors, "errors": errors},
                     ensure_ascii=False, indent=2))
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
