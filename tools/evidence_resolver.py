"""Deterministic repair of model-emitted evidence offsets, without inventing quotes."""
from collections import Counter

COLLECTIONS = ("attributes", "relations", "phenomena", "abstentions")


def resolve_evidence_offsets(prediction, case):
    """Repair offsets only when the exact quote uniquely exists in its declared source.

    If the current span is already exact it is retained. A quote occurring more than
    once is not guessed: the resolver leaves it untouched and reports ambiguity.
    """
    report = {"repaired": 0, "unresolved": []}
    if not isinstance(prediction, dict) or not isinstance(case, dict):
        return report
    sources = {"text": case.get("text", ""), "context": case.get("context", "")}
    for collection in COLLECTIONS:
        entries = prediction.get(collection, [])
        if not isinstance(entries, list):
            continue
        for entry_index, entry in enumerate(entries):
            if not isinstance(entry, dict):
                continue
            spans = entry.get("evidence_spans", [])
            if not isinstance(spans, list):
                continue
            for span_index, span in enumerate(spans):
                path = f"{collection}[{entry_index}].evidence_spans[{span_index}]"
                if not isinstance(span, dict):
                    report["unresolved"].append({"path": path, "reason": "span_not_object"})
                    continue
                source_name, quote = span.get("source"), span.get("quote")
                if source_name not in sources or not isinstance(quote, str) or not quote:
                    report["unresolved"].append({"path": path, "reason": "invalid_source_or_quote"})
                    continue
                source_text = sources[source_name]
                if not isinstance(source_text, str):
                    report["unresolved"].append({"path": path, "reason": "source_not_string"})
                    continue
                start, end = span.get("start"), span.get("end")
                if (isinstance(start, int) and not isinstance(start, bool)
                        and isinstance(end, int) and not isinstance(end, bool)
                        and 0 <= start < end <= len(source_text)
                        and source_text[start:end] == quote):
                    continue
                positions = []
                cursor = 0
                while True:
                    position = source_text.find(quote, cursor)
                    if position < 0:
                        break
                    positions.append(position)
                    cursor = position + 1
                if len(positions) == 1:
                    span["start"], span["end"] = positions[0], positions[0] + len(quote)
                    report["repaired"] += 1
                elif not positions:
                    report["unresolved"].append({"path": path, "reason": "quote_not_found"})
                else:
                    report["unresolved"].append({"path": path, "reason": "quote_ambiguous"})
    return report
