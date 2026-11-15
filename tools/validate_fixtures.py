#!/usr/bin/env python3
"""Validate v0.9 schemas and conformance fixture integrity (not model accuracy)."""
import hashlib
import json
import pathlib
from jsonschema import Draft202012Validator, FormatChecker

ROOT = pathlib.Path(__file__).resolve().parents[1]

def load(path):
    return json.loads(path.read_text(encoding="utf-8"))

def main():
    schema_paths = sorted((ROOT / "schemas").glob("surge-v0.9-*.schema.json"))
    if len(schema_paths) != 3:
        raise AssertionError(f"expected 3 v0.9 schemas, got {len(schema_paths)}")
    for path in schema_paths:
        schema = load(path)
        Draft202012Validator.check_schema(schema)
        Draft202012Validator(schema, format_checker=FormatChecker())
    suites = [load(ROOT / "tests/conformance/SURGE_v0.9_D01_D12.json"),
              load(ROOT / "tests/conformance/SURGE_v0.9_D13_D20.json")]
    cases = [c for suite in suites for c in suite["cases"]]
    ids = [c["id"] for c in cases]
    expected = [f"D{i:02d}" for i in range(1, 21)]
    if sorted(ids) != expected or len(set(ids)) != 20:
        raise AssertionError(f"fixture IDs mismatch: {ids}")
    computed = 0
    for case in suites[0]["cases"]:
        offset = 0
        for packet in case.get("packets", []):
            raw = packet["source_text"].encode("utf-8")
            r = packet.get("range", {})
            if r.get("start_offset") != offset or r.get("end_offset") != offset + len(raw):
                raise AssertionError(f"byte offsets mismatch in {case['id']}/{packet['packet_id']}")
            if r.get("source_range_digest") != hashlib.sha256(raw).hexdigest():
                raise AssertionError(f"SHA-256 mismatch in {case['id']}/{packet['packet_id']}")
            offset += len(raw)
            computed += 1
    for case in suites[0]["cases"]:
        seqs = [p["source_sequence"] for p in case.get("packets", [])]
        if seqs and (seqs != sorted(seqs) or len(seqs) != len(set(seqs))):
            raise AssertionError(f"non-monotonic packet sequence in {case['id']}")
    print(f"JSON Schema Draft 2020-12: {len(schema_paths)}/3 valid")
    print(f"Conformance fixture inventory: {len(cases)}/20 IDs valid")
    print(f"D01-D12 packet byte ranges/digests: {computed} verified")
    print("NOTE: fixture integrity is not a claim that all semantic Core behaviors are implemented.")

if __name__ == "__main__":
    main()
