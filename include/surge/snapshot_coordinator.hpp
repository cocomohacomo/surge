#pragma once
#include "surge/snapshot.hpp"
#include "surge/snapshot_store.hpp"
#include "surge/event_journal.hpp"
#include "surge/resource_budget.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
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

using SnapshotChunkWriter = std::function<bool(
    const char* data, std::size_t size, std::string* error)>;

class SnapshotDomain {
 public:
  virtual ~SnapshotDomain() = default;
  virtual std::string domain_id() const = 0;
  virtual std::string schema_id() const = 0;
  virtual bool capture(std::string* payload, std::string* error) const = 0;
  // Stream serialized payload chunks to the coordinator. The compatibility
  // default adapts capture(); domains should override this to avoid building
  // a full intermediate payload string.
  virtual bool capture_chunks(const SnapshotChunkWriter& writer,
                              std::string* error) const;
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
  // Maximum known simultaneous serialized capture staging (domain payload,
  // aggregate bundle and final envelope). This is a logical bound, not RSS.
  std::size_t max_capture_staging_bytes{64U * 1024U * 1024U};
  // Maximum single chunk accepted from a domain capture callback.
  std::size_t max_capture_chunk_bytes{64U * 1024U};
  // Maximum logical transient reservation for one restore operation.
  std::size_t max_restore_staging_bytes{64U * 1024U * 1024U};
};

class SnapshotCoordinator {
 public:
  explicit SnapshotCoordinator(SnapshotCoordinatorLimits limits = {},
                                ResourceBudget* budget = nullptr);
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
  bool restore_from_store(SnapshotStore& store, EventJournal& journal,
               const std::string& expected_context_id,
               std::uint64_t minimum_state_version,
               std::uint64_t minimum_journal_sequence,
               JournalTailLimits tail_limits, std::string* error);
  // Capture and durably save one complete checkpoint before atomically
  // replacing the journal with its compacted generation.
  bool compact(EventJournal& journal, SnapshotStore& store,
               const std::string& context_id, std::string* error);
 private:
  SnapshotCoordinatorLimits limits_;
  ResourceBudget* budget_{nullptr};
  std::vector<SnapshotDomain*> domains_;
};

}  // namespace surge
