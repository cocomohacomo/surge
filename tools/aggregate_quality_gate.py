#!/usr/bin/env python3
"""Fail-closed aggregate gate with immutable evidence identity checks."""
import json
import sys
from pathlib import Path

REQUIRED_IDENTITY = ("commit_sha", "profile_sha256", "corpus_sha256")


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def gate(core, recognition, expected):
    identities = {
        "core_integrity": {key: core.get(key) for key in REQUIRED_IDENTITY},
        "recognition_quality": {key: recognition.get(key) for key in REQUIRED_IDENTITY},
    }
    statuses = {
        "core_integrity": core.get("core_integrity_gate", core.get("gate_status", "NOT_RUN")),
        "recognition_quality": recognition.get("recognition_quality_gate", "NOT_RUN"),
    }
    problems = []
    for key in REQUIRED_IDENTITY:
        if not expected.get(key):
            problems.append("expected_identity:missing_" + key)
    for track, identity in identities.items():
        for key, value in identity.items():
            if not value:
                problems.append(track + ":missing_" + key)
            elif expected.get(key) and value != expected[key]:
                problems.append(track + ":mismatched_" + key)
    if identities["core_integrity"] != identities["recognition_quality"]:
        problems.append("track_identity_mismatch")
    for track, status in statuses.items():
        if status != "PASS":
            problems.append(track + ":" + str(status))
    passed = not problems
    return {
        "aggregate_gate": "PASS" if passed else "FAIL",
        "tracks": statuses,
        "identities": identities,
        "expected_identity": expected,
        "problems": sorted(set(problems)),
        "fail_closed": True,
    }


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: aggregate_quality_gate.py CORE.json RECOGNITION.json EXPECTED_IDENTITY.json OUTPUT.json")
    result = gate(load(sys.argv[1]), load(sys.argv[2]), load(sys.argv[3]))
    Path(sys.argv[4]).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["aggregate_gate"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
