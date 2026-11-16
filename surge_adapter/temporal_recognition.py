"""Conservative Japanese temporal-expression recognition for Claim validity proposals.

This module extracts candidates only. It never mutates Core state and never
supplies a timezone or reference instant that the caller did not provide.
"""
from __future__ import annotations

from dataclasses import dataclass, asdict
from datetime import date, datetime, time, timedelta, timezone
import re
from typing import Optional


@dataclass(frozen=True)
class TemporalCandidate:
    text: str
    role: str
    status: str
    valid_from: Optional[str]
    valid_until: Optional[str]
    precision: str
    confidence: float
    reason: str
    correction: bool = False

    def to_dict(self) -> dict:
        return asdict(self)


_DATE = r"(?P<year>20\d{2})年(?P<month>\d{1,2})月(?P<day>\d{1,2})日"
_FULL_DATE = re.compile(_DATE)
_FISCAL = re.compile(r"(?P<year>20\d{2})年度中")
_RELATIVE = re.compile(r"本日から(?P<days>\d{1,3})日間")
_MONTH_END = re.compile(r"(?:(?P<year>20\d{2})年)?(?P<month>\d{1,2})月末")
_AFTER_MONTH = re.compile(r"(?:(?P<year>20\d{2})年)?(?P<month>\d{1,2})月以降")
_CORRECTION = re.compile(r"(?:ではなく|ではなくて|に訂正|を訂正|から変更)")


def _offset(value: Optional[str], reference: Optional[datetime]) -> Optional[timezone]:
    if value:
        match = re.fullmatch(r"([+-])(\d{2}):(\d{2})", value)
        if not match:
            raise ValueError("timezone_offset must be ±HH:MM")
        hours, minutes = int(match.group(2)), int(match.group(3))
        if hours > 14 or minutes > 59 or (hours == 14 and minutes != 0):
            raise ValueError("timezone_offset is outside RFC3339 bounds")
        delta = timedelta(hours=hours, minutes=minutes)
        if match.group(1) == "-":
            delta = -delta
        return timezone(delta)
    if reference is not None and reference.tzinfo is not None and reference.utcoffset() is not None:
        return reference.tzinfo
    return None


def _stamp(day: date, zone: timezone) -> str:
    return datetime.combine(day, time.min, zone).isoformat(timespec="seconds")


def _date_from_match(match: re.Match) -> date:
    return date(int(match.group("year")), int(match.group("month")), int(match.group("day")))


def recognize_temporal_expressions(
    text: str,
    *,
    reference_datetime: Optional[datetime] = None,
    timezone_offset: Optional[str] = None,
) -> list[TemporalCandidate]:
    """Return conservative, ordered validity candidates found in Japanese text.

    timezone_offset is an explicit source timezone such as '+09:00'. An aware
    reference_datetime may supply its own timezone. Relative dates require an
    aware reference_datetime. Date-only expressions are normalized to local
    midnight only when a timezone is explicitly available.
    """
    if not isinstance(text, str):
        raise TypeError("text must be str")
    zone = _offset(timezone_offset, reference_datetime)
    candidates: list[TemporalCandidate] = []

    # Fiscal-year interval is a well-defined Japanese administrative convention.
    for m in _FISCAL.finditer(text):
        year = int(m.group("year"))
        if zone is None:
            candidates.append(TemporalCandidate(m.group(0), "INTERVAL", "NEEDS_TIMEZONE",
                None, None, "FISCAL_YEAR", 0.92, "Explicit timezone is required"))
        else:
            candidates.append(TemporalCandidate(m.group(0), "INTERVAL", "RESOLVED",
                _stamp(date(year, 4, 1), zone), _stamp(date(year + 1, 4, 1), zone),
                "FISCAL_YEAR", 0.92, "Japanese fiscal year: April 1 through next April 1"))

    # Exact date used as the beginning of a validity period.
    for m in _FULL_DATE.finditer(text):
        tail = text[m.end():m.end() + 16]
        if not re.match(r"\s*(?:から|より)(?:有効|適用|開始|効力|)", tail):
            continue
        if zone is None:
            candidates.append(TemporalCandidate(m.group(0), "VALID_FROM", "NEEDS_TIMEZONE",
                None, None, "DAY", 0.95, "Explicit timezone is required"))
        else:
            try:
                start = _stamp(_date_from_match(m), zone)
                candidates.append(TemporalCandidate(m.group(0), "VALID_FROM", "RESOLVED",
                    start, None, "DAY", 0.95, "Explicit date boundary"))
            except ValueError:
                candidates.append(TemporalCandidate(m.group(0), "VALID_FROM", "INVALID",
                    None, None, "DAY", 0.0, "Invalid calendar date"))

    # End-of-month expiry is represented as the next month's exclusive boundary.
    for m in _MONTH_END.finditer(text):
        year_text, month_text = m.group("year"), m.group("month")
        year, month = (int(year_text), int(month_text))
        if year_text is None:
            candidates.append(TemporalCandidate(m.group(0), "VALID_UNTIL", "NEEDS_YEAR",
                None, None, "MONTH", 0.55, "Year is absent; do not infer it"))
        elif not 1 <= month <= 12:
            candidates.append(TemporalCandidate(m.group(0), "VALID_UNTIL", "INVALID",
                None, None, "MONTH", 0.0, "Invalid month"))
        elif zone is None:
            candidates.append(TemporalCandidate(m.group(0), "VALID_UNTIL", "NEEDS_TIMEZONE",
                None, None, "MONTH", 0.85, "Explicit timezone is required"))
        else:
            next_month = date(year + (month == 12), (month % 12) + 1, 1)
            candidates.append(TemporalCandidate(m.group(0), "VALID_UNTIL", "RESOLVED",
                None, _stamp(next_month, zone), "MONTH", 0.85,
                "Month-end expiry converted to exclusive next-month boundary"))

    # "April onward" establishes a lower bound, not a closed interval.
    for m in _AFTER_MONTH.finditer(text):
        year_text, month_text = m.group("year"), m.group("month")
        if year_text is None:
            candidates.append(TemporalCandidate(m.group(0), "VALID_FROM", "NEEDS_YEAR",
                None, None, "MONTH", 0.55, "Year is absent; do not infer it"))
        elif not 1 <= int(month_text) <= 12:
            candidates.append(TemporalCandidate(m.group(0), "VALID_FROM", "INVALID",
                None, None, "MONTH", 0.0, "Invalid month"))
        elif zone is None:
            candidates.append(TemporalCandidate(m.group(0), "VALID_FROM", "NEEDS_TIMEZONE",
                None, None, "MONTH", 0.85, "Explicit timezone is required"))
        else:
            candidates.append(TemporalCandidate(m.group(0), "VALID_FROM", "RESOLVED",
                _stamp(date(int(year_text), int(month_text), 1), zone), None,
                "MONTH", 0.85, "Month boundary interpreted as first day"))

    # Relative periods are resolved only against a supplied aware reference instant.
    for m in _RELATIVE.finditer(text):
        if reference_datetime is None or reference_datetime.tzinfo is None or reference_datetime.utcoffset() is None:
            candidates.append(TemporalCandidate(m.group(0), "INTERVAL", "NEEDS_REFERENCE",
                None, None, "RELATIVE_DAYS", 0.0,
                "An aware reference_datetime is required"))
        else:
            start = reference_datetime
            end = start + timedelta(days=int(m.group("days")))
            candidates.append(TemporalCandidate(m.group(0), "INTERVAL", "RESOLVED",
                start.isoformat(timespec="seconds"), end.isoformat(timespec="seconds"),
                "RELATIVE_DAYS", 0.9, "Calculated from supplied reference_datetime"))

    if _CORRECTION.search(text):
        candidates = [
            TemporalCandidate(c.text, c.role, c.status, c.valid_from, c.valid_until,
                              c.precision, c.confidence, c.reason, True)
            for c in candidates
        ]
    # Keep source order stable, including ties.
    return sorted(candidates, key=lambda item: text.find(item.text))
