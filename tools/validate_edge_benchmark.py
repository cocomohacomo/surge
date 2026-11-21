#!/usr/bin/env python3
"""Validate and summarize a measured EventJournal edge resource profile."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path


REQUIRED = {
    "transactions", "warmup_transactions", "measured_transactions",
    "latency_samples", "payload_bytes", "max_identity_bytes",
    "identity_index_bytes", "append_p50_us", "append_p95_us",
    "append_p99_us", "replay_ms", "journal_bytes", "replayed_events",
    "peak_rss_kib", "peak_rss_supported", "peak_rss_metric",
}


def validate(report: dict, expected_transactions: int) -> list[str]:
    missing = sorted(REQUIRED - report.keys())
    if missing:
        raise ValueError("BENCHMARK_FIELDS_MISSING:" + ",".join(missing))
    if report["transactions"] != expected_transactions:
        raise ValueError("BENCHMARK_TRANSACTION_COUNT_MISMATCH")
    if report["replayed_events"] != expected_transactions:
        raise ValueError("BENCHMARK_REPLAY_COUNT_MISMATCH")
    if report["measured_transactions"] + report["warmup_transactions"] != expected_transactions:
        raise ValueError("BENCHMARK_WARMUP_ACCOUNTING_MISMATCH")
    if report["latency_samples"] <= 0 or report["journal_bytes"] <= 0:
        raise ValueError("BENCHMARK_EMPTY_MEASUREMENT")
    if report["identity_index_bytes"] > report["max_identity_bytes"]:
        raise ValueError("BENCHMARK_IDENTITY_BUDGET_EXCEEDED")
    if not report["peak_rss_supported"] or report["peak_rss_kib"] <= 0:
        raise ValueError("BENCHMARK_PEAK_RSS_UNAVAILABLE")
    for key in ("append_p50_us", "append_p95_us", "append_p99_us", "replay_ms"):
        value = report[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
            raise ValueError("BENCHMARK_METRIC_INVALID:" + key)
    if not (report["append_p50_us"] <= report["append_p95_us"] <= report["append_p99_us"]):
        raise ValueError("BENCHMARK_PERCENTILE_ORDER_INVALID")
    return [
        f"| Transactions | {report['transactions']:,} |",
        f"| Payload/event | {report['payload_bytes']:,} bytes |",
        f"| Append p50 / p95 / p99 | {report['append_p50_us']:.2f} / {report['append_p95_us']:.2f} / {report['append_p99_us']:.2f} µs |",
        f"| Replay | {report['replay_ms']:.2f} ms |",
        f"| Journal size | {report['journal_bytes']:,} bytes |",
        f"| Identity index | {report['identity_index_bytes']:,} / {report['max_identity_bytes']:,} bytes |",
        f"| Peak RSS | {report['peak_rss_kib']:,} KiB ({report['peak_rss_metric']}) |",
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--transactions", type=int, required=True)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()
    try:
        report = json.loads(args.report.read_text(encoding="utf-8"))
        rows = validate(report, args.transactions)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        raise SystemExit(str(exc))
    rendered = [
        "## SURGE EventJournal edge resource profile",
        "",
        "| Metric | Result |",
        "|---|---:|",
        *rows,
        "",
        "> CI-host measurements are regression evidence only. They are not a substitute for the declared deployment PC benchmark.",
        "",
    ]
    output = "\n".join(rendered)
    print(output, end="")
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as stream:
            stream.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
