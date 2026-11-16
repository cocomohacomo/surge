import unittest
from datetime import datetime, timezone, timedelta

from surge_adapter.temporal_recognition import recognize_temporal_expressions


class JapaneseTemporalRecognitionTests(unittest.TestCase):
    def test_explicit_start_requires_timezone(self):
        item = recognize_temporal_expressions("契約は2026年4月1日から有効")[0]
        self.assertEqual(item.status, "NEEDS_TIMEZONE")
        self.assertIsNone(item.valid_from)

    def test_explicit_start_with_timezone(self):
        item = recognize_temporal_expressions(
            "契約は2026年4月1日から有効", timezone_offset="+09:00")[0]
        self.assertEqual(item.status, "RESOLVED")
        self.assertEqual(item.valid_from, "2026-04-01T00:00:00+09:00")

    def test_month_end_without_year_is_not_guessed(self):
        item = recognize_temporal_expressions("3月末で失効")[0]
        self.assertEqual(item.status, "NEEDS_YEAR")

    def test_month_end_exclusive_boundary(self):
        item = recognize_temporal_expressions(
            "2026年3月末で失効", timezone_offset="+09:00")[0]
        self.assertEqual(item.valid_until, "2026-04-01T00:00:00+09:00")

    def test_after_month_requires_year(self):
        item = recognize_temporal_expressions("4月以降")[0]
        self.assertEqual(item.status, "NEEDS_YEAR")

    def test_after_month_with_year(self):
        item = recognize_temporal_expressions(
            "2026年4月以降", timezone_offset="+09:00")[0]
        self.assertEqual(item.valid_from, "2026-04-01T00:00:00+09:00")

    def test_fiscal_year_interval(self):
        item = recognize_temporal_expressions(
            "2025年度中", timezone_offset="+09:00")[0]
        self.assertEqual(item.valid_from, "2025-04-01T00:00:00+09:00")
        self.assertEqual(item.valid_until, "2026-04-01T00:00:00+09:00")

    def test_relative_requires_reference(self):
        item = recognize_temporal_expressions("本日から30日間")[0]
        self.assertEqual(item.status, "NEEDS_REFERENCE")

    def test_relative_uses_reference_timezone_and_date(self):
        ref = datetime(2026, 1, 1, 9, 30, tzinfo=timezone(timedelta(hours=9)))
        item = recognize_temporal_expressions("本日から30日間",
            reference_datetime=ref)[0]
        self.assertEqual(item.valid_from, "2026-01-01T09:30:00+09:00")
        self.assertEqual(item.valid_until, "2026-01-31T09:30:00+09:00")

    def test_correction_is_flagged_without_overwriting(self):
        items = recognize_temporal_expressions(
            "開始日は2026年4月1日ではなく2026年3月1日から有効",
            timezone_offset="+09:00")
        self.assertTrue(items)
        self.assertTrue(all(item.correction for item in items))

    def test_invalid_date_is_reported(self):
        item = recognize_temporal_expressions(
            "契約は2026年2月30日から有効", timezone_offset="+09:00")[0]
        self.assertEqual(item.status, "INVALID")


if __name__ == "__main__":
    unittest.main()
