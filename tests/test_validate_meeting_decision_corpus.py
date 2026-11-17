import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.validate_meeting_decision_corpus import validate_corpus


class FrozenGoldCorpusValidationTests(unittest.TestCase):
    def build_corpus(self, root: Path, leak=False, bad_span=False, bad_adjudication=False):
        cases, gold, annotations = [], [], []
        for index, split in enumerate(("train", "dev", "test"), 1):
            source = f"会議{index}で予算案を承認する。"
            quote = "予算案を承認"
            raw = source.encode("utf-8")
            start = raw.index(quote.encode("utf-8"))
            digest = hashlib.sha256(raw).hexdigest()
            meeting_id = "meeting-1" if leak and split == "test" else f"meeting-{index}"
            cases.append({"case_id": f"case-{index}", "meeting_id": meeting_id,
                          "split": split, "source_id": "source-1",
                          "source_text": source, "source_sha256": digest})
            item = {"action": "予算案を承認", "owner": None, "deadline": None,
                    "critical": True, "evidence_quote": quote,
                    "evidence_start": start, "evidence_end": start + len(quote.encode("utf-8"))}
            if bad_span and index == 1:
                item["evidence_end"] += 1
            adjudicated = dict(item)
            if bad_adjudication and index == 1:
                adjudicated["action"] = "別の決定"
            gold.append({"case_id": f"case-{index}", "source_sha256": digest,
                         "items": [item]})
            annotations.append({
                "case_id": f"case-{index}",
                "annotations": [
                    {"annotator_id": "ann-01", "items": [item]},
                    {"annotator_id": "ann-02", "items": [item]},
                ],
                "adjudication": {
                    "adjudicator_id": "adj-01", "status": "RESOLVED",
                    "items": [adjudicated],
                },
            })
        paths = {name: root / name for name in ("cases.jsonl", "gold.jsonl", "annotations.jsonl")}
        rows = {"cases.jsonl": cases, "gold.jsonl": gold, "annotations.jsonl": annotations}
        for name, records in rows.items():
            paths[name].write_text("".join(json.dumps(row, ensure_ascii=False) + "\n" for row in records), encoding="utf-8")
        split_counts = {
            "train": {"meetings": 1, "cases": 1},
            "dev": {"meetings": 1, "cases": 1},
            "test": {"meetings": 1, "cases": 1},
        }
        manifest = {
            "manifest_version": 1, "status": "FROZEN",
            "corpus_id": "synthetic-validator-fixture", "corpus_version": "0.0.0-test",
            "files": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in paths.items()},
            "source_rights": [{"source_id": "source-1", "rights_basis": "synthetic test fixture",
                               "redaction_policy": "not applicable"}],
            "annotator_ids": ["ann-01", "ann-02"],
            "adjudicator_ids": ["adj-01"],
            "annotation_guide_version": "test-1", "taxonomy_version": "test-1",
            "split_counts": split_counts,
            "minimums": {"meetings": 1, "items": 1, "critical_items": 1},
        }
        manifest_path = root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return paths, manifest_path

    def test_valid_frozen_corpus_is_eligible(self):
        with tempfile.TemporaryDirectory() as directory:
            paths, manifest = self.build_corpus(Path(directory))
            result = validate_corpus(paths["cases.jsonl"], paths["gold.jsonl"],
                                     paths["annotations.jsonl"], manifest)
            self.assertEqual(result["status"], "VALID_FROZEN_CORPUS")
            self.assertTrue(result["pilot_sample_ready"])
            self.assertEqual(result["meeting_count"], 3)

    def test_meeting_level_split_leakage_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            paths, manifest = self.build_corpus(Path(directory), leak=True)
            with self.assertRaisesRegex(ValueError, "MEETING_SPLIT_LEAKAGE"):
                validate_corpus(paths["cases.jsonl"], paths["gold.jsonl"],
                                paths["annotations.jsonl"], manifest)

    def test_gold_span_must_match_source_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            paths, manifest = self.build_corpus(Path(directory), bad_span=True)
            with self.assertRaisesRegex(ValueError, "GOLD_EVIDENCE_QUOTE_MISMATCH"):
                validate_corpus(paths["cases.jsonl"], paths["gold.jsonl"],
                                paths["annotations.jsonl"], manifest)

    def test_gold_must_equal_adjudicated_items(self):
        with tempfile.TemporaryDirectory() as directory:
            paths, manifest = self.build_corpus(Path(directory), bad_adjudication=True)
            with self.assertRaisesRegex(ValueError, "GOLD_DIFFERS_FROM_ADJUDICATION"):
                validate_corpus(paths["cases.jsonl"], paths["gold.jsonl"],
                                paths["annotations.jsonl"], manifest)

    def test_frozen_file_digest_is_enforced(self):
        with tempfile.TemporaryDirectory() as directory:
            paths, manifest = self.build_corpus(Path(directory))
            with paths["cases.jsonl"].open("a", encoding="utf-8") as stream:
                stream.write("{}\n")
            with self.assertRaisesRegex(ValueError, "CORPUS_FILE_DIGEST_MISMATCH"):
                validate_corpus(paths["cases.jsonl"], paths["gold.jsonl"],
                                paths["annotations.jsonl"], manifest)


if __name__ == "__main__":
    unittest.main()
