#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <utility>

namespace surge {
struct JournalEvent {
  std::string event_id;
  std::string kind;
  std::string payload;
};
struct JournalCommitResult {
  bool committed{false};
  bool duplicate{false};
  std::uint64_t state_version{0};
  std::string error;
};

// Append-only, commit-marker journal. A transaction becomes visible only after
// its COMMIT frame has been written and flushed. CRC32 detects accidental frame
// corruption; it is not a cryptographic authenticity mechanism.
class EventJournal {
 public:
  explicit EventJournal(std::string path);
  bool open();
  JournalCommitResult commit(const std::string& transaction_id,
                             const std::string& idempotency_key,
                             std::uint64_t expected_state_version,
                             const std::vector<JournalEvent>& events);
  std::uint64_t state_version() const;
  std::vector<JournalEvent> committed_events() const;
  std::string last_error() const;
 private:
  bool recover();
  bool append_frame(const std::string& payload);
  std::string path_;
  mutable std::mutex mutex_;
  bool opened_{false};
  std::uint64_t state_version_{0};
  std::vector<JournalEvent> events_;
  std::map<std::string, std::string> idempotency_;
  std::map<std::string, std::string> transaction_ids_;
  std::map<std::string, bool> event_ids_;
  std::string last_error_;
};
}  // namespace surge
