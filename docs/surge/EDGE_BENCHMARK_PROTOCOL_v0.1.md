# Edge Benchmark Protocol v0.1

## Purpose

Provide a reproducible, local benchmark for EventJournal on the actual target
business PC. CI benchmark smoke confirms that the harness builds and runs; it
is not a substitute for measurements on the deployment hardware.

## Build

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DSURGE_BUILD_BENCHMARKS=ON
cmake --build build-bench --config Release --target surge_journal_bench
```

## Run

```sh
./build-bench/surge_journal_bench 10000 128 0 8388608
```

Arguments are transaction count, payload bytes per event, active-tail event
limit, and retained identity-index byte budget. Use zero for an unlimited
individual limit. The example caps the identity index at 8 MiB.

The JSON report includes append p50/p95/p99, replay time, journal file size,
logical identity-index bytes, peak resident memory and the metric used by the
platform. Linux reports `getrusage.ru_maxrss` in KiB; macOS converts its byte
value to KiB; Windows reports peak working-set size in KiB. These values are
useful for platform-local comparison, but are not perfectly interchangeable.

## Required target-PC record

For each run, retain:

- exact SURGE commit SHA and build configuration;
- CPU model, physical/logical core count and RAM capacity;
- OS version, compiler and CMake version;
- transaction count, payload size and all configured limits;
- warm-up count, append latency percentiles, replay time and journal size;
- logical identity-index budget usage and peak resident-memory result;
- whether the run completed without exceeding the configured identity budget.

Run at least three repetitions after one warm-up run. Compare the maximum peak
resident memory and p95/p99 latency, not only the mean.

## Interpretation

The identity budget is conservative logical accounting, not allocator-exact
memory. Peak resident memory includes the whole benchmark process and runtime
overhead. A passing identity-budget test does not prove a process RSS cap.

This harness benchmarks EventJournal only. It does not measure SURGE semantic
recognition, packet scoring, Core graph updates, ASR inference, end-to-end RTF,
or model loading. Those require separate workload-specific benchmarks.
