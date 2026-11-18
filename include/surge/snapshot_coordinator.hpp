#pragma once
#include "surge/snapshot.hpp"
#include "surge/event_journal.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace surge {

// A restore is prepared without mutating live state. commit() must be noexcept;
// adapters must allocate and validate everything in prepare_restore().
class PreparedSnapshotRestore {
 public:
  virtual ~PreparedSnapshotRestore() = default;
  virtual void commit() noexcept = 0;
};

class SnapshotDomain {
 public:
  virtual ~SnapshotDomain() = default;
  virtual std::string domain_id() const = 0;
  virtual std::string schema_id() const = 0;
  virtual bool capture(std::string* payload, std::string* error) const = 0;
  virtual std::unique_ptr<PreparedSnapshotRestore> prepare_restore(
      const std::string& payload, std::string* error) = 0;
  // Build a detached state from the checkpoint and apply every later event
  // before publishing any domain. Adapters must preserve transaction order.
  virtual std::unique_ptr<PreparedSnapshotRestore> prepare_restore_with_tail(
      const std::string& payload, const std::vector<JournalEvent>& tail,
      std::string* error) {
    if (!tail.empty()) {
      if (error) *error = "SNAPSHOT_DOMAIN_TAIL_REPLAY_UNSUPPORTED";
      return nullptr;
    }
    return prepare_restore(payload, error);
  }
};

struct SnapshotCoordinatorLimits {
  SnapshotLimits envelope;
  std::size_t max_domains{64};
  std::size_t max_domain_payload_bytes{8U * 1024U * 1024U};
};

class SnapshotCoordinator {
 public:
  explicit SnapshotCoordinator(SnapshotCoordinatorLimits limits = {});
  bool register_domain(SnapshotDomain* domain, std::string* error);
  bool capture(const std::string& context_id, std::uint64_t state_version,
               std::uint64_t journal_sequence, std::string* bytes,
               std::string* error) const;
  bool restore(const std::string& bytes, const std::string& expected_context_id,
               std::uint64_t minimum_state_version,
               std::uint64_t minimum_journal_sequence,
               std::string* error);
  // Atomically prepare all registered domains from the snapshot plus a
  // bounded journal tail. The caller must quiesce all writers/readers.
  bool restore_with_tail(const std::string& bytes, EventJournal& journal,
               const std::string& expected_context_id,
               std::uint64_t minimum_state_version,
               std::uint64_t minimum_journal_sequence,
               JournalTailLimits tail_limits, std::string* error);
 private:
  SnapshotCoordinatorLimits limits_;
  std::vector<SnapshotDomain*> domains_;
};

}  // namespace surge
