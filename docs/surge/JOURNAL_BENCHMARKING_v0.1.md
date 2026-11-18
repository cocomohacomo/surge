# EventJournal benchmark baseline v0.1

Status: opt-in benchmark harness; no performance threshold is asserted.

## Build

Configure with `-DSURGE_BUILD_BENCHMARKS=ON` and build
`surge_journal_bench`.

## Run

`surge_journal_bench [transaction_count] [payload_bytes]`

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
