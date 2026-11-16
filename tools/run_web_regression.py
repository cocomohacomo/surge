"""Execute the exact 50-case Web regression corpus once its source records are imported.

The corpus is intentionally not synthesized by this tool. Missing source cases
are a hard error; the existing Japanese Scoring dataset is never substituted.
"""
from __future__ import annotations
import hashlib
import json
import pathlib
import sys
import tempfile
import unicodedata
from collections import Counter

from jsonschema import Draft202012Validator, FormatChecker

ROOT = pathlib.Path(__file__).resolve().parents[1]
DATA = ROOT / "tests/conformance/SURGE_WEB_REGRESSION_50_v0.1.json"
SCHEMA = ROOT / "tests/conformance/SURGE_WEB_REGRESSION_50_SCHEMA_v0.1.json"
sys.path.insert(0, str(ROOT))
from surge_adapter.web_document import WebDocumentIndex, WebDocumentRecognitionEngine
from surge_adapter.packet_ingest import Admission, normalize_source

class FixtureProvider:
    def __init__(self, candidate): self.candidate = candidate
    def recognize(self, text, context=None): return dict(self.candidate)

class FixtureCore:
    """Packet/Core seam stub; C++ atomic commit has its own integration suite."""
    def __init__(self): self.state_version = 0
    def admit_and_apply(self, packet, source, snapshot_digest, range_digest,
                        request_digest, revision, revision_sequence, revision_ref, delta):
        self.state_version += 1
        pos = packet["source_position"]
        admission = Admission("COMMITTED", packet["packet_id"], packet["context_id"],
            packet["source_id"], pos["source_generation"], pos["source_sequence"],
            "fixture-range", range_digest, request_digest)
        return admission, {"committed": True, "state_version": self.state_version}

def fail(message):
    raise SystemExit("WEB_REGRESSION_INVALID: " + message)

def main():
    if not DATA.is_file():
        fail("exact prior 50-case source corpus is not present; refusing to synthesize replacements")
    data = json.loads(DATA.read_text(encoding="utf-8"))
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    errors = sorted(Draft202012Validator(schema, format_checker=FormatChecker()).iter_errors(data),
                    key=lambda e: (list(map(str,e.absolute_path)),e.message))
    if errors:
        fail("schema: " + errors[0].message)
    cases = data["cases"]
    if len(cases) != 50 or {c["case_id"] for c in cases} != {f"WEB-{i:02d}" for i in range(1,51)}:
        fail("case IDs must be exactly WEB-01..WEB-50")
    counts = Counter(c["expected_decision"] for c in cases)
    if counts != Counter({"ACCEPT":41,"REVIEW":8,"ABSTAIN":1}):
        fail("expected aggregate must remain 41 ACCEPT / 8 REVIEW / 1 ABSTAIN")
    groups = Counter(c["source_group"] for c in cases)
    if groups != Counter({"MHLW":10,"MAFF":10,"MOE":10,"TOHOKU_UNIVERSITY":10,"MLIT_JMA":10}):
        fail("source groups must remain 10 cases each across the five original groups")

    actual = Counter()
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        index = WebDocumentIndex(pathlib.Path(tmp) / "web-regression.sqlite")
        core = FixtureCore()
        try:
            for case in cases:
                text = unicodedata.normalize("NFC", case["source_excerpt"])
                digest = hashlib.sha256(normalize_source(text)).hexdigest()
                if digest != case["source_digest"]:
                    failures.append(case["case_id"] + ":SOURCE_DIGEST_MISMATCH")
                    continue
                engine = WebDocumentRecognitionEngine(
                    FixtureProvider(case["provider_candidate"]), "web-regression",
                    case["source_group"], index, max_chunk_chars=max(6000,len(text)+1),
                    overlap_chars=0)
                result = engine.ingest(text, case["source_url"], case["publisher"], core,
                    case.get("title",""), case["acquired_at"], capture_status=case["capture_status"])
                if result.get("status") == "DUPLICATE":
                    failures.append(case["case_id"] + ":UNEXPECTED_DUPLICATE")
                    continue
                decision = result["decisions"][0]["decision"] if result["decisions"] else "NO_DECISION"
                actual[decision] += 1
                if decision != case["expected_decision"]:
                    failures.append(case["case_id"] + ":DECISION:" + decision)
                expected_claims = case["expected_claims"]
                got_claims = result["claims"]
                if len(expected_claims) != len(got_claims):
                    failures.append(case["case_id"] + ":CLAIM_COUNT")
                else:
                    for expected, got in zip(expected_claims,got_claims):
                        app = got["applicability"]
                        if expected["proposition"] != app["proposition"] or expected["polarity"] != app["polarity"]:
                            failures.append(case["case_id"] + ":CLAIM_MISMATCH")
                        if "topic" in expected and expected["topic"] != app["topic"]:
                            failures.append(case["case_id"] + ":TOPIC_MISMATCH")
                got_scopes = {(s["kind"],s["opener"]["exact_text"]) for s in result["delta"]["scope_spans"]}
                for expected in case["expected_scopes"]:
                    if (expected["kind"],expected["exact_text"]) not in got_scopes:
                        failures.append(case["case_id"] + ":SCOPE_MISSING:" + expected["kind"])
        finally:
            index.close()
    if failures:
        print("\n".join(failures))
        fail(f"{len(failures)} regression mismatches")
    if actual != counts:
        fail("actual decision aggregate differs from expected aggregate")
    print("PASS: 50 Web regression cases")
    print("Decisions:", dict(actual))
    print("Source groups:", dict(groups))

if __name__ == "__main__":
    main()
