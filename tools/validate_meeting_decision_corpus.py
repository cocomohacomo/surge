#!/usr/bin/env python3
"""Validate a frozen, independently adjudicated meeting-decision corpus."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import unicodedata
from collections import Counter


SPLITS = {"train", "dev", "test"}


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def read_jsonl(path: pathlib.Path) -> tuple[list[dict], str]:
    raw = path.read_bytes()
    try:
        decoded = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ValueError(f"{path}: INVALID_UTF8") from exc
    rows = []
    for line_no, line in enumerate(decoded.splitlines(), 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f"{path}:{line_no}: INVALID_JSON") from exc
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{line_no}: RECORD_NOT_OBJECT")
        rows.append(value)
    return rows, sha256_bytes(raw)


def unique_index(rows: list[dict], label: str) -> dict[str, dict]:
    result = {}
    for row in rows:
        case_id = row.get("case_id")
        if not isinstance(case_id, str) or not case_id or case_id in result:
            raise ValueError(f"{label}: CASE_ID_MISSING_OR_DUPLICATE")
        result[case_id] = row
    return result


def validate_item(item: object, source: str, case_id: str, label: str) -> None:
    if not isinstance(item, dict):
        raise ValueError(f"{case_id}: {label}_ITEM_NOT_OBJECT")
    action = item.get("action")
    owner, deadline, critical = item.get("owner"), item.get("deadline"), item.get("critical")
    if not isinstance(action, str) or not action.strip():
        raise ValueError(f"{case_id}: {label}_ACTION_INVALID")
    if owner is not None and not isinstance(owner, str):
        raise ValueError(f"{case_id}: {label}_OWNER_INVALID")
    if deadline is not None and not isinstance(deadline, str):
        raise ValueError(f"{case_id}: {label}_DEADLINE_INVALID")
    if not isinstance(critical, bool):
        raise ValueError(f"{case_id}: {label}_CRITICAL_INVALID")
    quote = item.get("evidence_quote")
    start, end = item.get("evidence_start"), item.get("evidence_end")
    if (not isinstance(quote, str) or not quote or
        isinstance(start, bool) or not isinstance(start, int) or
        isinstance(end, bool) or not isinstance(end, int) or
        start < 0 or end <= start):
        raise ValueError(f"{case_id}: {label}_EVIDENCE_SPAN_INVALID")
    raw = unicodedata.normalize("NFC", source).encode("utf-8")
    try:
        extracted = raw[start:end].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ValueError(f"{case_id}: {label}_EVIDENCE_OFFSET_NOT_UTF8_BOUNDARY") from exc
    if extracted != quote:
        raise ValueError(f"{case_id}: {label}_EVIDENCE_QUOTE_MISMATCH")


def validate_corpus(cases_path: pathlib.Path, gold_path: pathlib.Path,
                    annotations_path: pathlib.Path, manifest_path: pathlib.Path) -> dict:
    cases, cases_digest = read_jsonl(cases_path)
    gold, gold_digest = read_jsonl(gold_path)
    annotations, annotations_digest = read_jsonl(annotations_path)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("MANIFEST_INVALID_JSON_OR_UTF8") from exc
    if not isinstance(manifest, dict) or manifest.get("manifest_version") != 1:
        raise ValueError("MANIFEST_VERSION_INVALID")
    if manifest.get("status") != "FROZEN":
        raise ValueError("CORPUS_NOT_FROZEN")
    files = manifest.get("files")
    expected_files = {
        "cases.jsonl": cases_digest,
        "gold.jsonl": gold_digest,
        "annotations.jsonl": annotations_digest,
    }
    if not isinstance(files, dict) or any(files.get(name) != digest for name, digest in expected_files.items()):
        raise ValueError("CORPUS_FILE_DIGEST_MISMATCH")

    case_map = unique_index(cases, "CASES")
    gold_map = unique_index(gold, "GOLD")
    annotation_map = unique_index(annotations, "ANNOTATIONS")
    if not case_map or set(case_map) != set(gold_map) or set(case_map) != set(annotation_map):
        raise ValueError("CORPUS_CASE_SET_MISMATCH")

    rights_rows = manifest.get("source_rights")
    if not isinstance(rights_rows, list) or not rights_rows:
        raise ValueError("SOURCE_RIGHTS_REGISTER_MISSING")
    rights = {}
    for row in rights_rows:
        if not isinstance(row, dict):
            raise ValueError("SOURCE_RIGHTS_ENTRY_INVALID")
        source_id = row.get("source_id")
        if (not isinstance(source_id, str) or not source_id or source_id in rights or
            not isinstance(row.get("rights_basis"), str) or not row["rights_basis"].strip() or
            not isinstance(row.get("redaction_policy"), str) or not row["redaction_policy"].strip()):
            raise ValueError("SOURCE_RIGHTS_ENTRY_INCOMPLETE_OR_DUPLICATE")
        rights[source_id] = row

    annotator_values = manifest.get("annotator_ids")
    adjudicator_values = manifest.get("adjudicator_ids")
    if (not isinstance(annotator_values, list) or not all(isinstance(x, str) and x for x in annotator_values) or
        not isinstance(adjudicator_values, list) or not all(isinstance(x, str) and x for x in adjudicator_values)):
        raise ValueError("ANNOTATION_ROSTER_MISSING")
    declared_annotators = set(annotator_values)
    declared_adjudicators = set(adjudicator_values)
    if not declared_annotators or not declared_adjudicators:
        raise ValueError("ANNOTATION_ROSTER_MISSING")
    if not isinstance(manifest.get("annotation_guide_version"), str) or not manifest["annotation_guide_version"]:
        raise ValueError("ANNOTATION_GUIDE_VERSION_MISSING")
    if not isinstance(manifest.get("taxonomy_version"), str) or not manifest["taxonomy_version"]:
        raise ValueError("TAXONOMY_VERSION_MISSING")

    meeting_splits = {}
    split_cases = Counter()
    split_meetings = {split: set() for split in SPLITS}
    total_items = critical_items = 0
    agreement_tp = agreement_fp = agreement_fn = 0
    field_matches = {"owner": 0, "deadline": 0, "critical": 0, "evidence_span": 0}
    field_totals = {"owner": 0, "deadline": 0, "critical": 0, "evidence_span": 0}
    observed_annotators, observed_adjudicators = set(), set()

    for case_id, case in case_map.items():
        meeting_id, split, source_id = case.get("meeting_id"), case.get("split"), case.get("source_id")
        source = case.get("source_text")
        if not isinstance(meeting_id, str) or not meeting_id:
            raise ValueError(f"{case_id}: MEETING_ID_MISSING")
        if split not in SPLITS:
            raise ValueError(f"{case_id}: SPLIT_INVALID")
        if source_id not in rights:
            raise ValueError(f"{case_id}: SOURCE_RIGHTS_NOT_REGISTERED")
        if not isinstance(source, str) or not source:
            raise ValueError(f"{case_id}: SOURCE_TEXT_MISSING")
        digest = sha256_bytes(unicodedata.normalize("NFC", source).encode("utf-8"))
        if case.get("source_sha256") != digest or gold_map[case_id].get("source_sha256") != digest:
            raise ValueError(f"{case_id}: SOURCE_DIGEST_MISMATCH")
        previous = meeting_splits.setdefault(meeting_id, split)
        if previous != split:
            raise ValueError(f"MEETING_SPLIT_LEAKAGE:{meeting_id}")
        split_cases[split] += 1
        split_meetings[split].add(meeting_id)

        gold_items = gold_map[case_id].get("items")
        if not isinstance(gold_items, list):
            raise ValueError(f"{case_id}: GOLD_ITEMS_NOT_ARRAY")
        for item in gold_items:
            validate_item(item, source, case_id, "GOLD")
        total_items += len(gold_items)
        critical_items += sum(1 for item in gold_items if item["critical"] is True)

        record = annotation_map[case_id]
        independent = record.get("annotations")
        adjudication = record.get("adjudication")
        if not isinstance(independent, list) or len(independent) != 2:
            raise ValueError(f"{case_id}: EXACTLY_TWO_INDEPENDENT_ANNOTATIONS_REQUIRED")
        annotator_ids = []
        for annotation in independent:
            if not isinstance(annotation, dict):
                raise ValueError(f"{case_id}: ANNOTATION_RECORD_INVALID")
            annotator_id = annotation.get("annotator_id")
            items = annotation.get("items")
            if not isinstance(annotator_id, str) or not annotator_id or not isinstance(items, list):
                raise ValueError(f"{case_id}: ANNOTATION_PROVENANCE_INVALID")
            if annotator_id in annotator_ids:
                raise ValueError(f"{case_id}: ANNOTATORS_NOT_INDEPENDENT")
            annotator_ids.append(annotator_id)
            observed_annotators.add(annotator_id)
            for item in items:
                validate_item(item, source, case_id, "ANNOTATION")
        left_items, right_items = independent[0]["items"], independent[1]["items"]
        left = {unicodedata.normalize("NFC", item["action"]).casefold().strip(): item
                for item in left_items}
        right = {unicodedata.normalize("NFC", item["action"]).casefold().strip(): item
                 for item in right_items}
        shared = set(left) & set(right)
        agreement_tp += len(shared)
        agreement_fp += len(set(left) - set(right))
        agreement_fn += len(set(right) - set(left))
        for action in shared:
            a, b = left[action], right[action]
            for field in ("owner", "deadline", "critical"):
                field_totals[field] += 1
                field_matches[field] += a.get(field) == b.get(field)
            field_totals["evidence_span"] += 1
            field_matches["evidence_span"] += (
                a.get("evidence_start") == b.get("evidence_start") and
                a.get("evidence_end") == b.get("evidence_end") and
                a.get("evidence_quote") == b.get("evidence_quote")
            )
        if not isinstance(adjudication, dict) or adjudication.get("status") != "RESOLVED":
            raise ValueError(f"{case_id}: ADJUDICATION_NOT_RESOLVED")
        adjudicator_id = adjudication.get("adjudicator_id")
        adjudicated_items = adjudication.get("items")
        if (not isinstance(adjudicator_id, str) or not adjudicator_id or
            adjudicator_id in annotator_ids or not isinstance(adjudicated_items, list)):
            raise ValueError(f"{case_id}: ADJUDICATION_PROVENANCE_INVALID")
        observed_adjudicators.add(adjudicator_id)
        for item in adjudicated_items:
            validate_item(item, source, case_id, "ADJUDICATED")
        if canonical(adjudicated_items) != canonical(gold_items):
            raise ValueError(f"{case_id}: GOLD_DIFFERS_FROM_ADJUDICATION")

    if set(split_meetings) != SPLITS or any(not split_meetings[s] for s in SPLITS):
        raise ValueError("ALL_TRAIN_DEV_TEST_SPLITS_REQUIRED")
    if not observed_annotators.issubset(declared_annotators):
        raise ValueError("UNDECLARED_ANNOTATOR")
    if not observed_adjudicators.issubset(declared_adjudicators):
        raise ValueError("UNDECLARED_ADJUDICATOR")

    split_counts = {
        split: {"meetings": len(split_meetings[split]), "cases": split_cases[split]}
        for split in sorted(SPLITS)
    }
    if manifest.get("split_counts") != split_counts:
        raise ValueError("MANIFEST_SPLIT_COUNTS_MISMATCH")
    minimums = manifest.get("minimums", {})
    if not isinstance(minimums, dict):
        raise ValueError("MINIMUM_SAMPLE_THRESHOLDS_INVALID")
    required = {
        "meetings": minimums.get("meetings", 100),
        "items": minimums.get("items", 500),
        "critical_items": minimums.get("critical_items", 50),
    }
    if any(isinstance(v, bool) or not isinstance(v, int) or v < 1 for v in required.values()):
        raise ValueError("MINIMUM_SAMPLE_THRESHOLDS_INVALID")
    meeting_count = len(meeting_splits)
    thresholds = manifest.get("inter_annotator_agreement")
    if not isinstance(thresholds, dict):
        raise ValueError("INTER_ANNOTATOR_AGREEMENT_THRESHOLDS_MISSING")
    threshold_names = {
        "minimum_action_f1": "action_f1",
        "minimum_owner_agreement": "owner",
        "minimum_deadline_agreement": "deadline",
        "minimum_criticality_agreement": "critical",
        "minimum_evidence_span_agreement": "evidence_span",
    }
    parsed_thresholds = {}
    for threshold_name in threshold_names:
        value = thresholds.get(threshold_name)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 <= value <= 1:
            raise ValueError("INTER_ANNOTATOR_AGREEMENT_THRESHOLD_INVALID:" + threshold_name)
        parsed_thresholds[threshold_name] = float(value)
    action_precision = agreement_tp / (agreement_tp + agreement_fp) if agreement_tp + agreement_fp else None
    action_recall = agreement_tp / (agreement_tp + agreement_fn) if agreement_tp + agreement_fn else None
    action_f1 = (2 * action_precision * action_recall / (action_precision + action_recall)
                 if action_precision is not None and action_recall is not None and
                 action_precision + action_recall else None)
    agreements = {
        "action_f1": action_f1,
        "owner": field_matches["owner"] / field_totals["owner"] if field_totals["owner"] else None,
        "deadline": field_matches["deadline"] / field_totals["deadline"] if field_totals["deadline"] else None,
        "critical": field_matches["critical"] / field_totals["critical"] if field_totals["critical"] else None,
        "evidence_span": field_matches["evidence_span"] / field_totals["evidence_span"] if field_totals["evidence_span"] else None,
    }
    agreement_gate = "PASS"
    for threshold_name, metric_name in threshold_names.items():
        metric = agreements[metric_name]
        if metric is None:
            agreement_gate = "INCONCLUSIVE"
            break
        if metric < parsed_thresholds[threshold_name]:
            agreement_gate = "FAIL"
    sample_ready = (meeting_count >= required["meetings"] and total_items >= required["items"] and
                    critical_items >= required["critical_items"])
    ready = sample_ready and agreement_gate == "PASS"
    return {
        "corpus_id": manifest.get("corpus_id"),
        "corpus_version": manifest.get("corpus_version"),
        "status": "VALID_FROZEN_CORPUS",
        "meeting_count": meeting_count,
        "case_count": len(case_map),
        "item_count": total_items,
        "critical_item_count": critical_items,
        "split_counts": split_counts,
        "minimums": required,
        "pilot_sample_ready": sample_ready,
        "inter_annotator_agreement": {
            "gate": agreement_gate,
            "action_f1": agreements["action_f1"],
            "owner_agreement": agreements["owner"],
            "deadline_agreement": agreements["deadline"],
            "criticality_agreement": agreements["critical"],
            "evidence_span_agreement": agreements["evidence_span"],
            "thresholds": parsed_thresholds,
            "matched_actions": agreement_tp,
            "unmatched_left_actions": agreement_fp,
            "unmatched_right_actions": agreement_fn,
        },
        "evaluation_gate": (
            "ELIGIBLE_FOR_HELD_OUT_EVALUATION" if ready else
            "INTER_ANNOTATOR_AGREEMENT_" + agreement_gate if sample_ready else
            "INCOMPLETE_PILOT_SAMPLE"
        ),
        "digests": expected_files,
        "note": "Corpus integrity validation is not a model-quality result.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", required=True, type=pathlib.Path)
    parser.add_argument("--gold", required=True, type=pathlib.Path)
    parser.add_argument("--annotations", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    args = parser.parse_args()
    result = validate_corpus(args.cases, args.gold, args.annotations, args.manifest)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
