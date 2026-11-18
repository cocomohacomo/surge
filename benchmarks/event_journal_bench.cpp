#include "surge/event_journal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

std::size_t parse_size(const char* value, std::size_t fallback,
                        std::size_t maximum) {
  if (!value) return fallback;
  try {
    const auto parsed = std::stoull(value);
    if (parsed == 0 || parsed > maximum) return fallback;
    return static_cast<std::size_t>(parsed);
  } catch (...) {
    return fallback;
  }
}

double percentile(std::vector<double> values, double fraction) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(
      std::ceil(fraction * static_cast<double>(values.size()))) - 1;
  return values[std::min(index, values.size() - 1)];
}
}  // namespace

int main(int argc, char** argv) {
  const std::size_t transactions =
      parse_size(argc > 1 ? argv[1] : nullptr, 1000, 1000000);
  const std::size_t payload_bytes =
      parse_size(argc > 2 ? argv[2] : nullptr, 128, 4U * 1024U * 1024U);
  const std::size_t warmup = std::min<std::size_t>(50, transactions / 10);
  const auto stamp = Clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
      ("surge-journal-bench-" + std::to_string(stamp));
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) {
    std::cerr << "benchmark temp directory creation failed\n";
    return 2;
  }
  const auto path = root / "benchmark.sgj";
  std::vector<double> latency_us;
  latency_us.reserve(transactions - warmup);
  const std::string payload(payload_bytes, 'x');

  {
    surge::EventJournal journal(path.string());
    if (!journal.open()) {
      std::cerr << "journal open failed: " << journal.last_error() << "\n";
      std::filesystem::remove_all(root, ec);
      return 2;
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
        std::cerr << "journal commit failed: " << result.error << "\n";
        std::filesystem::remove_all(root, ec);
        return 3;
      }
      if (i >= warmup) {
        latency_us.push_back(
            std::chrono::duration<double, std::micro>(stop - start).count());
      }
    }
  }

  const auto bytes = std::filesystem::file_size(path, ec);
  if (ec) {
    std::cerr << "journal file size unavailable\n";
    std::filesystem::remove_all(root, ec);
    return 4;
  }

  const auto replay_start = Clock::now();
  std::size_t replayed = 0;
  {
    surge::EventJournal recovered(path.string());
    if (!recovered.open()) {
      std::cerr << "journal replay failed: " << recovered.last_error() << "\n";
      std::filesystem::remove_all(root, ec);
      return 5;
    }
    replayed = recovered.committed_events().size();
    if (recovered.state_version() != transactions || replayed != transactions) {
      std::cerr << "journal replay count mismatch\n";
      std::filesystem::remove_all(root, ec);
      return 6;
    }
  }
  const auto replay_stop = Clock::now();
  const auto replay_ms =
      std::chrono::duration<double, std::milli>(replay_stop - replay_start).count();

  std::cout << "{\n"
            << "  \"transactions\": " << transactions << ",\n"
            << "  \"warmup_transactions\": " << warmup << ",\n"
            << "  \"measured_transactions\": " << latency_us.size() << ",\n"
            << "  \"payload_bytes\": " << payload_bytes << ",\n"
            << "  \"append_p50_us\": " << percentile(latency_us, 0.50) << ",\n"
            << "  \"append_p95_us\": " << percentile(latency_us, 0.95) << ",\n"
            << "  \"append_p99_us\": " << percentile(latency_us, 0.99) << ",\n"
            << "  \"replay_ms\": " << replay_ms << ",\n"
            << "  \"journal_bytes\": " << bytes << ",\n"
            << "  \"replayed_events\": " << replayed << "\n"
            << "}\n";

  std::filesystem::remove_all(root, ec);
  return 0;
}
