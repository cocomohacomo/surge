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
  // Runtime watermark assigned by EventJournal after a transaction commits.
  // It is reconstructed from the durable COMMIT frame and is not user input.
  std::uint64_t state_version{0};
};
struct EventJournalLimits {
  std::size_t max_events{0};
  std::size_t max_payload_bytes{0};
  std::size_t max_transactions{0};
};
struct JournalCommitResult {
  bool committed{false};
  bool duplicate{false};
  std::uint64_t state_version{0};
  std::string error;
};
struct JournalTailLimits {
  // Tail reads are bounded by default for edge deployments.
  std::size_t max_events{65536};
  std::size_t max_total_bytes{16U * 1024U * 1024U};
};
struct JournalCheckpointInfo {
  bool compacted{false};
  std::uint64_t base_state_version{0};
  std::string context_id;
  std::string snapshot_digest;
};
struct JournalTailResult {
  bool ok{false};
  std::uint64_t after_state_version{0};
  std::uint64_t current_state_version{0};
  std::size_t total_bytes{0};
  std::vector<JournalEvent> events;
  std::string error;
};

// Append-only, commit-marker journal. CRC32 detects accidental frame corruption;
// it is not a cryptographic authenticity mechanism.
class EventJournal {
 public:
  using AppendFrameGate = std::function<bool(std::size_t frame_index)>;
  using SyncFrameGate = std::function<bool(std::size_t frame_index)>;
  explicit EventJournal(std::string path);
  EventJournal(std::string path, EventJournalLimits limits);
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
  // After compaction this is the active tail only. Rebuild domain state from
  // SnapshotStore with SnapshotCoordinator::restore_from_store.
  std::vector<JournalEvent> committed_events() const;
  // Returns all committed events after a transaction-state watermark. The
  // result is all-or-nothing and rejects gaps, future watermarks, and limits.
  JournalTailResult committed_tail(std::uint64_t after_state_version,
                                   JournalTailLimits limits = {}) const;
  std::string last_error() const;
  JournalCheckpointInfo checkpoint_info() const;
 private:
  friend class SnapshotCoordinator;
  bool compact_after_checkpoint(const std::string& snapshot_bytes,
                                const std::string& expected_context_id,
                                std::string* error);
  bool recover();
  bool acquire_writer_lock();
  void release_writer_lock();
  bool append_frame(const std::string& payload);
  std::string path_;
  EventJournalLimits limits_{};
  std::size_t retained_payload_bytes_{0};
  std::intptr_t lock_handle_{-1};
  AppendFrameGate append_frame_gate_;
  SyncFrameGate sync_frame_gate_;
  std::size_t append_frame_count_{0};
  mutable std::mutex mutex_;
  bool opened_{false};
  std::uint64_t state_version_{0};
  std::uint64_t checkpoint_base_state_version_{0};
  std::string checkpoint_context_id_;
  std::string checkpoint_snapshot_digest_;
  bool compacted_{false};
  std::vector<JournalEvent> events_;
  std::map<std::string, std::string> idempotency_;
  std::map<std::string, std::string> transaction_ids_;
  std::map<std::string, bool> event_ids_;
  std::string last_error_;
};
}  // namespace surge
