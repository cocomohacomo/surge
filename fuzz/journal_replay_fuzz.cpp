#include "surge/event_journal.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  constexpr std::size_t kMaximumInput = 1024U * 1024U;
  if (size > kMaximumInput || (data == nullptr && size != 0)) return 0;

  static std::atomic<std::uint64_t> sequence{0};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
      ("surge-journal-fuzz-" + std::to_string(stamp) + "-" +
       std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) return 0;

  const auto path = root / "candidate.sgj";
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
      std::filesystem::remove_all(root, ec);
      return 0;
    }
    output.write("SURGEJ1\n", 8);
    if (size != 0) {
      output.write(reinterpret_cast<const char*>(data),
                   static_cast<std::streamsize>(size));
    }
    output.flush();
    if (!output) {
      std::filesystem::remove_all(root, ec);
      return 0;
    }
  }

  {
    surge::EventJournal journal(path.string());
    if (journal.open()) {
      const auto events = journal.committed_events();
      (void)events.size();
      (void)journal.state_version();
    }
  }
  std::filesystem::remove_all(root, ec);
  return 0;
}
