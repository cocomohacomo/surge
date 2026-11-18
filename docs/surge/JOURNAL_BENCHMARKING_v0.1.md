# EventJournal benchmark baseline v0.1

Status: opt-in benchmark harness; no performance threshold is asserted.

## Build

Configure with `-DSURGE_BUILD_BENCHMARKS=ON` and build
`surge_journal_bench`.

## Run

`surge_journal_bench [transaction_count] [payload_bytes] [max_events]`

Defaults are 1,000 transactions and 128 payload bytes. Transaction count is
bounded to 1,000,000 and payload size to 4 MiB. The harness performs real
EventJournal commits, including the configured stable-storage synchronization
barriers. It excludes up to 50 initial transactions as warm-up, then reports
append p50/p95/p99 latency, full journal replay time, journal file size, and
replayed event count as JSON.

## Interpretation

- Append latency includes transaction framing, file append/flush, and the
  platform's `fsync` or `FlushFileBuffers` barrier.
- Replay time includes journal verification and recovery, not domain-level
  reconstruction in every reducer.
- Results depend on filesystem, storage device, virtualization, power policy,
  operating system, build mode, and background load.
- The CI benchmark is a smoke test only. It does not enforce latency limits and
  is not a substitute for target-device measurements.
- This benchmark does not measure recognition-model inference, RTF, model
  loading, peak RSS, or 30MB model integration.

## Next measurement stage

On target hardware, run at least 10 warm-up and 1,000 measured transactions
with representative event payload distributions. Record OS, CPU, storage,
filesystem, build flags, payload distribution, p50/p95/p99, throughput,
replay time, and journal size. Model inference and end-to-end streaming RTF
must be benchmarked separately once the model adapter is integrated.


## Edge-PC memory-budget mode

The optional third argument sets EventJournal's retained-event ceiling. Zero
(the default) means unlimited for compatibility. A non-zero limit is checked
both during journal replay and before a transaction is durably appended. A
replay that exceeds the configured ceiling fails closed with
`JOURNAL_RESOURCE_LIMIT_EXCEEDED`; it does not silently truncate or compact
history. The benchmark reports `peak_rss_kib` on Linux using VmHWM and -1 on
platforms where this measurement is not currently implemented.

Example bounded run:

```sh
./build-bench/surge_journal_bench 10000 128 10000
```

The event ceiling is a logical guardrail, not a hard RSS cap. Event payloads,
container overhead, allocator fragmentation, reducer state, and the rest of the
SURGE process also consume memory. Use target-device RSS measurements to set a
safe ceiling. The current C++ journal still retains committed events and
idempotency/transaction/event identity maps up to the configured limits; this
change prevents unbounded growth when a non-zero profile is selected, but does
not implement snapshot/compaction.
