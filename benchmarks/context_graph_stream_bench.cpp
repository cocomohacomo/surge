#include "surge/budgeted_context_graph.hpp"
#include "surge/context_graph.hpp"
#include "surge/resource_budget.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#ifdef __linux__
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#elif defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

bool parse_count(const char* text, std::size_t& value) {
  if (!text) { value = 100000; return true; }
  try {
    std::size_t used = 0;
    const auto parsed = std::stoull(text, &used);
    if (used != std::string(text).size() || parsed == 0 || parsed > 2000000)
      return false;
    value = static_cast<std::size_t>(parsed);
    return true;
  } catch (...) { return false; }
}

double percentile(std::vector<double> values, double fraction) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(
      std::ceil(fraction * static_cast<double>(values.size()))) - 1;
  return values[std::min(index, values.size() - 1)];
}

long long current_rss_bytes() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return -1;
  return static_cast<long long>(counters.WorkingSetSize);
#elif defined(__linux__)
  std::ifstream statm("/proc/self/statm");
  long long total_pages = 0, resident_pages = 0;
  if (!(statm >> total_pages >> resident_pages)) return -1;
  return resident_pages * static_cast<long long>(sysconf(_SC_PAGESIZE));
#else
  return -1;
#endif
}

long long peak_rss_bytes() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return -1;
  return static_cast<long long>(counters.PeakWorkingSetSize);
#elif defined(__linux__) || defined(__APPLE__)
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
#if defined(__APPLE__)
  return static_cast<long long>(usage.ru_maxrss);
#else
  return static_cast<long long>(usage.ru_maxrss) * 1024LL;
#endif
#else
  return -1;
#endif
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 3) {
    std::cerr << "usage: surge_context_graph_stream_bench [events] [recall_interval]\n";
    return 1;
  }
  std::size_t events = 0, recall_interval = 100;
  if (!parse_count(argc > 1 ? argv[1] : nullptr, events) ||
      !parse_count(argc > 2 ? argv[2] : nullptr, recall_interval)) {
    std::cerr << "invalid event count or recall interval (1..2000000)\n";
    return 1;
  }

  const auto root = std::filesystem::temp_directory_path() /
      ("surge-context-stream-" +
       std::to_string(Clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) return 2;

  surge::ResourceBudgetLimits limits;
  limits.resident_bytes = 200U * 1024U * 1024U;
  limits.transient_bytes = 32U * 1024U * 1024U;
  limits.retained_events = events + 16;
  limits.retained_clusters = 4096;
  limits.decision_candidates = 1024;
  limits.decision_evidence = 8192;
  limits.total_byte_limit = 200U * 1024U * 1024U;
  surge::ResourceBudget budget(limits);
  surge::ContextGraph graph((root / "stream.sgj").string());
  if (!graph.open()) {
    std::cerr << "ContextGraph open failed\n";
    std::filesystem::remove_all(root, ec);
    return 3;
  }
  surge::BudgetedContextGraph bounded(graph, budget);
  std::string budget_error;
  if (!bounded.recover(budget_error)) {
    std::cerr << "logical budget recovery failed: " << budget_error << "\n";
    std::filesystem::remove_all(root, ec);
    return 4;
  }

  const long long rss_start = current_rss_bytes();
  std::vector<double> latency_us, recall_latency_us;
  latency_us.reserve(std::min<std::size_t>(events, 200000));
  recall_latency_us.reserve(std::min<std::size_t>(
      (events + recall_interval - 1) / recall_interval, 200000));
  std::size_t candidate_total = 0, candidate_max = 0, recall_samples = 0, completed = 0;
  std::size_t peak_logical_resident = bounded.accounted_resident_bytes();

  for (std::size_t i = 0; i < events; ++i) {
    const auto started = Clock::now();
    const std::string suffix = std::to_string(i);
    surge::SemanticDelta delta;
    delta.delta_id = "stream-delta-" + suffix;
    delta.idempotency_key = delta.delta_id;
    delta.expected_state_version = graph.state_version();
    if (i == 0) {
      delta.nodes.push_back({{"stream-root", surge::NodeKind::Cluster,
                              "CLASS=STREAM;PARTITION=BENCHMARK", {}}});
    }
    const std::string member_id = "stream-member-" + suffix;
    delta.nodes.push_back({{member_id, surge::NodeKind::Evidence,
                            "stream event cognition context token " + suffix, {}}});
    delta.relations.push_back({{"stream-edge-" + suffix,
        surge::RelationKind::Contains, "stream-root", member_id, 0.75}});
    const auto committed = bounded.apply(delta);
    if (!committed.committed) {
      std::cerr << "stream commit failed at " << i << ": " << committed.error << "\n";
      std::filesystem::remove_all(root, ec);
      return 5;
    }

    if (i % recall_interval == 0) {
      const auto recall_started = Clock::now();
      const auto candidates = bounded.recall_cognition_candidates(
          "stream-query", {"stream", "cognition", "context"}, 16);
      const auto recall_stopped = Clock::now();
      candidate_total += candidates.size();
      candidate_max = std::max(candidate_max, candidates.size());
      ++recall_samples;
      recall_latency_us.push_back(std::chrono::duration<double, std::micro>(
          recall_stopped - recall_started).count());
    }
    peak_logical_resident = std::max(peak_logical_resident,
                                      bounded.accounted_resident_bytes());
    ++completed;
    const auto stopped = Clock::now();
    if (i >= std::min<std::size_t>(100, events / 10))
      latency_us.push_back(std::chrono::duration<double, std::micro>(
          stopped - started).count());
  }

  const long long rss_end = current_rss_bytes();
  const long long peak_rss = peak_rss_bytes();
  const auto logical = budget.snapshot();
  const double growth_per_1000 = (rss_start >= 0 && rss_end >= 0 && completed)
      ? static_cast<double>(rss_end - rss_start) * 1000.0 /
            static_cast<double>(completed) : -1.0;
  std::cout << "{\n"
            << "  \"events_requested\": " << events << ",\n"
            << "  \"events_committed\": " << completed << ",\n"
            << "  \"state_version\": " << graph.state_version() << ",\n"
            << "  \"recall_interval_events\": " << recall_interval << ",\n"
            << "  \"recall_samples\": " << recall_samples << ",\n"
            << "  \"candidate_total\": " << candidate_total << ",\n"
            << "  \"candidate_max_per_recall\": " << candidate_max << ",\n"
            << "  \"operation_p50_us\": " << percentile(latency_us, 0.50) << ",\n"
            << "  \"recall_p50_us\": " << percentile(recall_latency_us, 0.50) << ",\n"
            << "  \"recall_p95_us\": " << percentile(recall_latency_us, 0.95) << ",\n"
            << "  \"recall_p99_us\": " << percentile(recall_latency_us, 0.99) << ",\n"
            << "  \"operation_p95_us\": " << percentile(latency_us, 0.95) << ",\n"
            << "  \"operation_p99_us\": " << percentile(latency_us, 0.99) << ",\n"
            << "  \"rss_start_bytes\": " << rss_start << ",\n"
            << "  \"rss_end_bytes\": " << rss_end << ",\n"
            << "  \"peak_rss_bytes\": " << peak_rss << ",\n"
            << "  \"rss_growth_bytes_per_1000_events\": " << growth_per_1000 << ",\n"
            << "  \"rss_current_supported\": " << (rss_start >= 0 && rss_end >= 0 ? "true" : "false") << ",\n"
            << "  \"logical_resident_accounted_bytes\": " << bounded.accounted_resident_bytes() << ",\n"
            << "  \"logical_resident_peak_accounted_bytes\": " << peak_logical_resident << ",\n"
            << "  \"logical_resident_limit_bytes\": " << limits.resident_bytes << ",\n"
            << "  \"logical_transient_limit_bytes\": " << limits.transient_bytes << ",\n"
            << "  \"logical_total_limit_bytes\": " << limits.total_byte_limit << ",\n"
            << "  \"logical_total_used_bytes\": " << logical.total_byte_used << ",\n"
            << "  \"logical_vs_rss_are_distinct_metrics\": true\n"
            << "}\n";
  std::filesystem::remove_all(root, ec);
  return 0;
}
