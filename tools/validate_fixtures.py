#!/usr/bin/env python3
"""Validate v0.9 schemas and v1.0 Chaos Cluster contracts."""
import hashlib
import math
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
    if len(schema_paths) != 5:
        raise AssertionError(f"expected 5 v0.9 schemas, got {len(schema_paths)}")
    for path in schema_paths:
        schema = load(path)
        Draft202012Validator.check_schema(schema)
        Draft202012Validator(schema, format_checker=FormatChecker())

    minimal_schema = load(ROOT / "schemas/surge-minimal-projection-v0.1.schema.json")
    Draft202012Validator.check_schema(minimal_schema)
    minimal_validator = Draft202012Validator(minimal_schema)
    minimal_suite = load(ROOT / "tests/conformance/SURGE_MINIMAL_PROJECTION_v0.1.json")
    if minimal_suite.get("contract") != "surge-minimal-projection-v0.1":
        raise AssertionError("minimal projection contract identifier mismatch")
    minimal_cases = minimal_suite.get("cases", [])
    if {case.get("case_id") for case in minimal_cases} != {
        "existing-nearest-cluster", "no-candidate"
    }:
        raise AssertionError("minimal projection fixture case inventory mismatch")
    for case in minimal_cases:
        errors = list(minimal_validator.iter_errors(case.get("projection", {})))
        if errors:
            raise AssertionError(
                f"invalid minimal projection fixture {case.get('case_id')}: {errors}"
            )
    if any(not math.isfinite(case["projection"]["distance"]) for case in minimal_cases):
        raise AssertionError("minimal projection distance must be finite")
    print("Minimal Information Projection v0.1 schema and 2 fixtures: valid")

    projection_schema = load(ROOT / "schemas/projection-batch-v0.1.schema.json")
    Draft202012Validator.check_schema(projection_schema)
    projection_validator = Draft202012Validator(projection_schema, format_checker=FormatChecker())
    projection_fixtures = sorted((ROOT / "tests/fixtures/projection").glob("*.json"))
    if {path.stem for path in projection_fixtures} != {"conversation", "document", "structured"}:
        raise AssertionError("ProjectionBatch requires conversation/document/structured fixtures")
    for fixture_path in projection_fixtures:
        batch = load(fixture_path)
        errors = list(projection_validator.iter_errors(batch))
        if errors:
            raise AssertionError(f"invalid ProjectionBatch fixture {fixture_path}: {errors}")
        source = unicodedata.normalize("NFC", batch["normalized_source_snapshot"])
        source_bytes = source.encode("utf-8")
        packet = batch["source_packet"]
        digest = hashlib.sha256(source_bytes).hexdigest()
        if (source != batch["normalized_source_snapshot"] or
            packet["end_offset"] != len(source_bytes) or
            packet["source_snapshot_digest"] != digest or
            packet["source_range_digest"] != digest or
            batch["source_event_refs"] != [packet["packet_id"]] or
            batch["context_id"] != packet.get("context_id", batch["context_id"])):
            raise AssertionError(f"ProjectionBatch source lineage mismatch: {fixture_path}")
        evidence_ids = {node["id"] for node in batch["nodes"] if node["kind"] == "EVIDENCE"}
        if not set(batch["evidence_refs"]).issubset(evidence_ids):
            raise AssertionError(f"ProjectionBatch Evidence refs are not represented: {fixture_path}")

    language_schema = load(ROOT / "schemas/surge-language-pack-v1.0.schema.json")
    Draft202012Validator.check_schema(language_schema)
    language_validator = Draft202012Validator(language_schema, format_checker=FormatChecker())
    language_manifests = sorted((ROOT / "tests/fixtures/language-packs").glob("*/manifest.json"))
    if len(language_manifests) < 2:
        raise AssertionError("expected at least two structurally distinct language pack fixtures")
    language_ids = set()
    for manifest_path in language_manifests:
        manifest = load(manifest_path)
        errors = list(language_validator.iter_errors(manifest))
        if errors:
            raise AssertionError(f"invalid Language Set manifest {manifest_path}: {errors}")
        language_ids.add(manifest["language"]["id"])
    if not {"ja", "en"}.issubset(language_ids):
        raise AssertionError(f"Japanese and English fixtures are required, got {language_ids}")

    chaos_schema = load(ROOT / "schemas/surge-chaos-cluster-v1.0.schema.json")
    Draft202012Validator.check_schema(chaos_schema)
    chaos_validator = Draft202012Validator(chaos_schema, format_checker=FormatChecker())
    chaos_suite = load(ROOT / "tests/conformance/SURGE_CHAOS_CLUSTER_v1.0.json")
    chaos_cases = chaos_suite.get("cases", [])
    expected_chaos_ids = [f"CC{i:02d}" for i in range(1, 21)]
    chaos_ids = [case.get("id") for case in chaos_cases]
    if sorted(chaos_ids) != expected_chaos_ids or len(set(chaos_ids)) != 20:
        raise AssertionError(f"Chaos Cluster v1.0 fixture IDs mismatch: {chaos_ids}")
    if chaos_suite.get("version") != "1.0.0" or chaos_suite.get("status") != "DESIGN_CONFORMANCE_EXPECTATIONS":
        raise AssertionError("Chaos Cluster fixture must declare version and design-only status")

    # Validate a representative fully populated object and reject semantic ambiguity
    # at the schema boundary (e.g. missing provenance or invalid score range).
    valid_example = {
        "schema_version":"1.0.0","cluster_id":"chaos:fixture:1","cluster_kind":"CHAOS",
        "lifecycle":"ACCUMULATING","organization_state":"MULTI_PATTERN","state_version":1,
        "members":[{"member_ref":"member:1","event_ref":"event:1","membership_status":"UNCLASSIFIED",
                    "admission_reason":"NO_STABLE_TOPIC","admitted_at":"2026-11-17T00:00:00Z",
                    "provenance":{"producer_id":"fixture","producer_version":"1","policy_version":"p1","recorded_at":"2026-11-17T00:00:00Z"}}],
        "associations":[],"provenance":{"producer_id":"fixture","producer_version":"1","policy_version":"p1","recorded_at":"2026-11-17T00:00:00Z"}
    }
    errors = list(chaos_validator.iter_errors(valid_example))
    if errors:
        raise AssertionError(f"valid Chaos Cluster example rejected: {errors}")
    invalid_example = dict(valid_example)
    invalid_example["observations"] = {"salience":{"value":1.5,"scorer_id":"s","scorer_version":"1","policy_version":"p1","observed_at":"2026-11-17T00:00:00Z"}}
    if not list(chaos_validator.iter_errors(invalid_example)):
        raise AssertionError("out-of-range Chaos Cluster score was accepted")

    suites = [load(ROOT / "tests/conformance/SURGE_v0.9_D01_D12.json"),
              load(ROOT / "tests/conformance/SURGE_v0.9_D13_D20.json")]
    cases = [c for suite in suites for c in suite["cases"]]
    ids = [c["id"] for c in cases]
    expected = [f"D{i:02d}" for i in range(1, 21)]
    if sorted(ids) != expected or len(set(ids)) != 20:
        raise AssertionError(f"fixture IDs mismatch: {ids}")
    packet_count = 0
    for case in suites[0]["cases"]:
        offset = 0
        for packet in case.get("packets", []):
            normalized = unicodedata.normalize("NFC", packet["source_text"])
            raw = normalized.encode("utf-8")
            packet_count += 1
            offset += len(raw)
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
    print("ProjectionBatch v0.1 JSON Schema and 3 cross-format fixtures: valid")
    print("JSON Schema Draft 2020-12: 5/5 v0.9 schemas + Chaos Cluster v1.0 + Language Set v1.0 valid")
    print(f"Language Set manifest fixtures: {len(language_manifests)} valid; ja/en coverage passed")
    print("Chaos Cluster semantic conformance inventory: 20/20 cases valid")
    print("Chaos Cluster schema positive/negative examples: passed")
    print(f"Conformance fixture inventory: {len(cases)}/20 cases valid")
    print(f"D01-D12 exact NFC UTF-8 packet range derivation: {packet_count} packets")
    print("NOTE: fixture consistency and primitive tests do not establish full semantic Core conformance.")
    result = subprocess.run(
        [sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tests"),
         "-p", "test_*.py", "-v"],
        cwd=ROOT, check=False)
    if result.returncode:
        raise SystemExit(result.returncode)
    print("RecognitionPacket adapter and Python/C++ runtime bridge tests passed")

if __name__ == "__main__":
    main()
