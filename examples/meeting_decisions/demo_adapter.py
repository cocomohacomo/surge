"""Deterministic smoke-test adapter for explicitly structured meeting notes.

Input convention (no inference):
決定: <decision text>
担当: <owner>
期限: <YYYY-MM-DD>

This is a parser smoke test, not a competitive baseline or semantic model.
"""
import hashlib
import re
import unicodedata


def predict(case):
    source = unicodedata.normalize("NFC", case["source_text"])
    decision = re.search(r"^決定:\s*(.+?)\s*$", source, re.MULTILINE)
    if not decision:
        return {"decision": "ABSTAIN", "items": []}
    owner_match = re.search(r"^担当:\s*(.+?)\s*$", source, re.MULTILINE)
    deadline_match = re.search(r"^期限:\s*(\d{4}-\d{2}-\d{2})\s*$", source, re.MULTILINE)
    quote = decision.group(1)
    raw = source.encode("utf-8")
    start = raw.index(quote.encode("utf-8"))
    end = start + len(quote.encode("utf-8"))
    digest = hashlib.sha256(raw).hexdigest()
    item = {
        "action": quote,
        "owner": owner_match.group(1) if owner_match else None,
        "deadline": deadline_match.group(1) if deadline_match else None,
        "evidence_quote": quote,
        "evidence_start": start,
        "evidence_end": end,
        "source_sha256": digest,
    }
    return {"decision": "ACCEPT" if owner_match and deadline_match else "REVIEW",
            "items": [item]}
