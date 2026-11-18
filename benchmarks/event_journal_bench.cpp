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

#ifdef __linux__
#include <unistd.h>
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

long peak_rss_kib() {
#ifdef __linux__
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == "VmHWM:") {
      long value = 0;
      std::string unit;
      status >> value >> unit;
      return value;
    }
    std::string rest;
    std::getline(status, rest);
  }
#endif
  return -1;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 4) {
    std::cerr << "usage: surge_journal_bench [transactions] [payload_bytes] [max_events]\n";
    return 1;
  }
  std::size_t transactions = 1000, payload_bytes = 128, max_events = 0;
  if (!parse_size(argc > 1 ? argv[1] : nullptr, 1000, 1000000, &transactions) ||
      transactions == 0 ||
      !parse_size(argc > 2 ? argv[2] : nullptr, 128, 4U * 1024U * 1024U, &payload_bytes) ||
      payload_bytes == 0 ||
      !parse_size(argc > 3 ? argv[3] : nullptr, 0, 1000000, &max_events)) {
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
  const long rss_kib = peak_rss_kib();

  std::cout << "{\n"
            << "  \"transactions\": " << transactions << ",\n"
            << "  \"warmup_transactions\": " << warmup << ",\n"
            << "  \"measured_transactions\": " << measured << ",\n"
            << "  \"latency_samples\": " << latency_us.size() << ",\n"
            << "  \"payload_bytes\": " << payload_bytes << ",\n"
            << "  \"max_events\": " << max_events << ",\n"
            << "  \"append_p50_us\": " << percentile(latency_us, 0.50) << ",\n"
            << "  \"append_p95_us\": " << percentile(latency_us, 0.95) << ",\n"
            << "  \"append_p99_us\": " << percentile(latency_us, 0.99) << ",\n"
            << "  \"replay_ms\": " << replay_ms << ",\n"
            << "  \"journal_bytes\": " << bytes << ",\n"
            << "  \"replayed_events\": " << replayed << ",\n"
            << "  \"peak_rss_kib\": " << rss_kib << "\n"
            << "}\n";
  std::filesystem::remove_all(root, ec);
  return 0;
}
