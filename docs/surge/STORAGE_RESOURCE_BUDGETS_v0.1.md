# Storage Resource Budgets v0.1

## Purpose

SURGE persistence must expose hard admission limits for the state retained by an edge process. Limits are opt-in for compatibility: zero means unlimited. Production profiles should set explicit values based on the application's measured workload and available RAM.

## EventJournal limits

EventJournalLimits now includes:

| Limit | Scope | After J2 compaction |
|---|---|---|
| max_events | Committed event payloads retained in the active journal tail | Counts active tail only |
| max_payload_bytes | Payload bytes retained in the active tail | Counts active tail only |
| max_transactions | Unique transaction IDs retained for exact duplicate/reuse checks | Lifetime count |
| max_event_ids | Unique event IDs retained for exact duplicate/reuse checks | Lifetime count |

A commit that would exceed any configured limit is rejected before writing its BEGIN frame. Recovery applies the same checks and fails closed if the on-disk journal exceeds the configured profile. A compacted J2 checkpoint manifest is checked against the configured lifetime limits before the journal is opened.

max_event_ids is separate from max_events: one transaction may contain many events, and compaction removes payload history but deliberately preserves all event identities. This limit therefore provides a direct upper bound on the number of entries in the event-ID uniqueness index. It does not expire IDs or weaken exactly-once behavior.

## Memory interpretation

These are logical cardinality and payload bounds, not a byte-accurate RSS guarantee. The standard-library map node, allocator, key length, fingerprint storage, pending transaction buffers, snapshot staging, and domain state also consume memory. max_transactions and max_event_ids should be selected conservatively and validated with the benchmark on the target compiler, standard library, and operating system.

Do not infer a process RSS cap from these limits. The application must also bound domain state, snapshot bundle size, tail replay size, and concurrent staging.

## Benchmark telemetry

With SURGE_BUILD_BENCHMARKS=ON, surge_journal_bench emits JSON including append p50/p95/p99, replay duration, journal bytes, current RSS where supported, and peak RSS where supported.

- Linux: current RSS from VmRSS, peak RSS from VmHWM.
- Windows: current and peak working set through process memory counters.
- macOS: peak RSS through getrusage; current RSS is reported as unavailable.
- Unsupported metrics are reported as -1.

CI benchmark runs are smoke tests, not edge-PC performance certification. Record the exact CPU, RAM, OS, compiler, build mode, transaction count, payload size, and configured limits for target-device measurements.

## Backup retention safety

The journal .bak remains a complete replay source through the last compaction watermark. The snapshot primary and mirrored .bak both represent the compaction checkpoint. Automatic deletion is intentionally not performed: pruning must be an explicit maintenance operation that proves the active compacted journal and at least one valid snapshot generation remain a recoverable pair, and must coordinate with readers of the backup file.

Until that coordinated pruning operation is implemented, retain the journal backup. This preserves recovery safety at the cost of additional disk usage. Do not manually delete .bak files while the application may be running or recovering.

## Recommended deployment profile

1. Set nonzero max_transactions and max_event_ids based on expected lifetime identity volume.
2. Set max_events and max_payload_bytes for the active tail.
3. Set JournalTailLimits and snapshot envelope/domain limits independently.
4. Run the benchmark with representative transaction and payload distributions.
5. Measure peak RSS on the actual edge PC under concurrent application load.
6. Treat a resource-limit rejection as an explicit backpressure/maintenance condition; do not silently discard identity history.
