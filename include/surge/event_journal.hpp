#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
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
// its COMMIT frame has been written and synchronized to stable storage. CRC32 detects accidental frame
// corruption; it is not a cryptographic authenticity mechanism.
class EventJournal {
 public:
  using AppendFrameGate = std::function<bool(std::size_t frame_index)>;
  using SyncFrameGate = std::function<bool(std::size_t frame_index)>;
  explicit EventJournal(std::string path);
  ~EventJournal();
  EventJournal(std::string path, AppendFrameGate append_frame_gate);
  EventJournal(std::string path, AppendFrameGate append_frame_gate,
               SyncFrameGate sync_frame_gate);
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
  bool acquire_writer_lock();
  void release_writer_lock();
  bool append_frame(const std::string& payload);
  std::string path_;
  std::intptr_t lock_handle_{-1};
  AppendFrameGate append_frame_gate_;
  SyncFrameGate sync_frame_gate_;
  std::size_t append_frame_count_{0};
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
