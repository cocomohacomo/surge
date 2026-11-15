#!/usr/bin/env python3
"""Validate v0.9 schemas and derive exact fixture packet ranges/digests."""
import hashlib
import json
import pathlib
import subprocess
import sys
import unicodedata
from jsonschema import Draft202012Validator, FormatChecker

ROOT = pathlib.Path(__file__).resolve().parents[1]

def load(path):
    return json.loads(path.read_text(encoding="utf-8"))

def main():
    schema_paths = sorted((ROOT / "schemas").glob("surge-v0.9-*.schema.json"))
    if len(schema_paths) != 4:
        raise AssertionError(f"expected 4 v0.9 schemas, got {len(schema_paths)}")
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
    packet_count = 0
    derived_ranges = {}
    for case in suites[0]["cases"]:
        offset = 0
        for packet in case.get("packets", []):
            normalized = unicodedata.normalize("NFC", packet["source_text"])
            raw = normalized.encode("utf-8")
            derived_ranges[packet["packet_id"]] = {
                "start_offset": offset,
                "end_offset": offset + len(raw),
                "offset_basis": suites[0]["offset_basis"],
                "source_range_digest": hashlib.sha256(raw).hexdigest(),
            }
            offset += len(raw)
            packet_count += 1
        seqs = [p["source_sequence"] for p in case.get("packets", [])]
        if seqs and (seqs != sorted(seqs) or len(seqs) != len(set(seqs))):
            raise AssertionError(f"non-monotonic packet sequence in {case['id']}")
        if case["id"] in [f"D{i:02d}" for i in range(1, 7)]:
            packets = case["packets"]
            if len(packets) != 2 or packets[0]["finalization"] != "CONTINUATION" or packets[1]["finalization"] != "FINAL":
                raise AssertionError(f"invalid split-scope fixture {case['id']}")
            if case["expected"]["final_scope_status"] != "CLOSED":
                raise AssertionError(f"missing closed-scope expectation {case['id']}")
    by_id = {c["id"]: c for c in cases}
    if by_id["D07"]["expected"]["integration_order"] != [1, 2, 3]:
        raise AssertionError("D07 causal integration order mismatch")
    if by_id["D08"]["expected"]["gap_sequences"] != [11]:
        raise AssertionError("D08 gap fixture mismatch")
    if by_id["D09"]["expected"]["context_complete"] is not False:
        raise AssertionError("D09 must be incomplete")
    if by_id["D10"]["expected"]["old_result_reused"] is not False:
        raise AssertionError("D10 must reject stale reuse")
    if by_id["D11"]["expected"]["coverage"] >= by_id["D11"]["preconditions"]["minimum_coverage"]:
        raise AssertionError("D11 must fail the minimum coverage threshold")
    if by_id["D12"]["expected"]["duplicate_units"] != 0:
        raise AssertionError("D12 duplicate-unit expectation missing")
    if by_id["D13"]["expected"]["gap_status"] != "AUTHORIZED_SKIP":
        raise AssertionError("D13 authorized-skip expectation missing")
    if by_id["D14"]["expected"]["error"] != "GAP_SKIP_UNAUTHORIZED":
        raise AssertionError("D14 authorization failure expectation missing")
    if by_id["D15"]["expected"]["skip_audit_preserved"] is not True:
        raise AssertionError("D15 must preserve skip audit")
    if by_id["D16"]["expected"]["new_evidence_count"] != 0:
        raise AssertionError("D16 duplicate retry must not add Evidence")
    if by_id["D17"]["expected"]["error"] != "RANGE_CONTENT_CONFLICT":
        raise AssertionError("D17 conflict expectation missing")
    if by_id["D18"]["expected"]["old_record_mutated"] is not False:
        raise AssertionError("D18 must preserve old revision")
    if by_id["D19"]["expected"]["gate"] != "INCONCLUSIVE":
        raise AssertionError("D19 zero denominator must be inconclusive")
    if by_id["D20"]["expected"]["error"] != "BENCHMARK_PROFILE_IMMUTABLE":
        raise AssertionError("D20 profile immutability expectation missing")
    print(f"JSON Schema Draft 2020-12: {len(schema_paths)}/3 schemas valid")
    print(f"Conformance fixture inventory: {len(cases)}/20 cases valid")
    print(f"D01-D12 exact NFC UTF-8 ranges and SHA-256 digests derived: {packet_count} packets")
    print(f"D01-D20 fixture contract assertions: 20/20 structurally consistent")
    result = subprocess.run(
        [sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tests"),
         "-p", "test_*.py", "-v"],
        cwd=ROOT, check=False)
    if result.returncode:
        raise SystemExit(result.returncode)
    print("RecognitionPacket adapter and Python/C++ runtime bridge tests passed")
    print("NOTE: fixture consistency and primitive tests do not establish full semantic Core conformance.")

if __name__ == "__main__":
    main()
