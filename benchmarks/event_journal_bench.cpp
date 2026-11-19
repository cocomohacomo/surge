#include "surge/event_journal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

bool parse_size(const char* value, std::size_t fallback, std::size_t maximum,
                std::size_t* output) {
  if (!value) { *output = fallback; return true; }
  try {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed);
    if (consumed != std::string(value).size() || parsed > maximum) return false;
    *output = static_cast<std::size_t>(parsed);
    return true;
  } catch (...) { return false; }
}

double percentile(const std::vector<double>& values, double fraction) {
  if (values.empty()) return 0.0;
  auto sorted = values;
  std::sort(sorted.begin(), sorted.end());
  const auto index = static_cast<std::size_t>(
      std::ceil(fraction * static_cast<double>(sorted.size()))) - 1;
  return sorted[std::min(index, sorted.size() - 1)];
}

long long peak_rss_kib() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
      sizeof(counters))) return -1;
  return static_cast<long long>(counters.PeakWorkingSetSize / 1024U);
#elif defined(__linux__) || defined(__APPLE__)
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
#if defined(__APPLE__)
  return static_cast<long long>(usage.ru_maxrss / 1024);
#else
  return static_cast<long long>(usage.ru_maxrss);
#endif
#else
  return -1;
#endif
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 5) {
    std::cerr << "usage: surge_journal_bench [transactions] [payload_bytes] [max_events] [max_identity_bytes]\n";
    return 1;
  }
  std::size_t transactions = 1000, payload_bytes = 128, max_events = 0, max_identity_bytes = 0;
  if (!parse_size(argc > 1 ? argv[1] : nullptr, 1000, 1000000, &transactions) ||
      transactions == 0 ||
      !parse_size(argc > 2 ? argv[2] : nullptr, 128, 4U * 1024U * 1024U, &payload_bytes) ||
      payload_bytes == 0 ||
      !parse_size(argc > 3 ? argv[3] : nullptr, 0, 1000000, &max_events) ||
      !parse_size(argc > 4 ? argv[4] : nullptr, 0, 1024U * 1024U * 1024U, &max_identity_bytes)) {
    std::cerr << "invalid benchmark argument\n";
    return 1;
  }
  const std::size_t warmup = std::min<std::size_t>(50, transactions / 10);
  const auto stamp = Clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
      ("surge-journal-bench-" + std::to_string(stamp));
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) { std::cerr << "benchmark temp directory creation failed\n"; return 2; }
  const auto path = root / "benchmark.sgj";
  std::vector<double> latency_us;
  const std::size_t measured = transactions - warmup;
  const std::size_t sample_stride = std::max<std::size_t>(1, measured / 8192);
  const std::string payload(payload_bytes, 'x');
  surge::EventJournalLimits limits;
  limits.max_events = max_events;
  limits.max_identity_bytes = max_identity_bytes;
  std::size_t identity_index_bytes = 0;

  {
    surge::EventJournal journal(path.string(), limits);
    if (!journal.open()) {
      std::cerr << "journal open failed: " << journal.last_error() << "\n";
      std::filesystem::remove_all(root, ec); return 2;
    }
    for (std::size_t i = 0; i < transactions; ++i) {
      const auto suffix = std::to_string(i);
      const auto start = Clock::now();
      const auto result = journal.commit(
          "bench-tx-" + suffix, "bench-idem-" + suffix,
          journal.state_version(),
          {{"bench-event-" + suffix, "BENCHMARK_EVENT", payload}});
      const auto stop = Clock::now();
      if (!result.committed) {
        std::cerr << "journal commit failed at transaction " << i
                  << ": " << result.error << "\n";
        std::filesystem::remove_all(root, ec); return 3;
      }
      if (i >= warmup && ((i - warmup) % sample_stride == 0))
        latency_us.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
    }
    identity_index_bytes = journal.identity_memory_bytes();
  }

  const auto bytes = std::filesystem::file_size(path, ec);
  if (ec) { std::cerr << "journal file size unavailable\n"; std::filesystem::remove_all(root, ec); return 4; }
  const auto replay_start = Clock::now();
  std::size_t replayed = 0;
  {
    surge::EventJournal recovered(path.string(), limits);
    if (!recovered.open()) {
      std::cerr << "journal replay failed: " << recovered.last_error() << "\n";
      std::filesystem::remove_all(root, ec); return 5;
    }
    replayed = recovered.committed_events().size();
    if (recovered.state_version() != transactions || replayed != transactions) {
      std::cerr << "journal replay count mismatch\n";
      std::filesystem::remove_all(root, ec); return 6;
    }
  }
  const auto replay_stop = Clock::now();
  const auto replay_ms = std::chrono::duration<double, std::milli>(
      replay_stop - replay_start).count();
  const long long rss_kib = peak_rss_kib();

  std::cout << "{\n"
            << "  \"transactions\": " << transactions << ",\n"
            << "  \"warmup_transactions\": " << warmup << ",\n"
            << "  \"measured_transactions\": " << measured << ",\n"
            << "  \"latency_samples\": " << latency_us.size() << ",\n"
            << "  \"payload_bytes\": " << payload_bytes << ",\n"
            << "  \"max_events\": " << max_events << ",\n"
            << "  \"max_identity_bytes\": " << max_identity_bytes << ",\n"
            << "  \"identity_index_bytes\": " << identity_index_bytes << ",\n"
            << "  \"append_p50_us\": " << percentile(latency_us, 0.50) << ",\n"
            << "  \"append_p95_us\": " << percentile(latency_us, 0.95) << ",\n"
            << "  \"append_p99_us\": " << percentile(latency_us, 0.99) << ",\n"
            << "  \"replay_ms\": " << replay_ms << ",\n"
            << "  \"journal_bytes\": " << bytes << ",\n"
            << "  \"replayed_events\": " << replayed << ",\n"
            << "  \"peak_rss_kib\": " << rss_kib << ",\n"
            << "  \"peak_rss_supported\": " << (rss_kib >= 0 ? "true" : "false") << "\n"
            << "}\n";
  std::filesystem::remove_all(root, ec);
  return 0;
}
