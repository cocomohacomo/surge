#include "surge/journal_backup_retention.hpp"
#include "surge/snapshot_coordinator.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; return 1; } } while (0)

namespace {
class IntRestore final : public surge::PreparedSnapshotRestore {
 public:
  IntRestore(int* target, int value) : target_(target), value_(value) {}
  void commit() noexcept override { *target_ = value_; }
 private:
  int* target_;
  int value_;
};
class IntDomain final : public surge::SnapshotDomain {
 public:
  int value{0};
  std::string domain_id() const override { return "int"; }
  std::string schema_id() const override { return "int.v1"; }
  bool capture(std::string* payload, std::string* error) const override {
    if (!payload) { if (error) *error = "OUTPUT_REQUIRED"; return false; }
    *payload = std::to_string(value);
    if (error) error->clear();
    return true;
  }
  std::unique_ptr<surge::PreparedSnapshotRestore> prepare_restore(
      const std::string& payload, std::string* error) override {
    try {
      std::size_t used = 0;
      const int parsed = std::stoi(payload, &used);
      if (used != payload.size()) throw std::invalid_argument("tail");
      if (error) error->clear();
      return std::make_unique<IntRestore>(&value, parsed);
    } catch (...) {
      if (error) *error = "INVALID_INT";
      return nullptr;
    }
  }
  std::unique_ptr<surge::PreparedSnapshotRestore> prepare_restore_with_tail(
      const std::string& payload, const std::vector<surge::JournalEvent>& tail,
      std::string* error) override {
    auto staged = prepare_restore(payload, error);
    if (!staged) return nullptr;
    // This test uses an empty post-compaction tail. The compacted checkpoint
    // and both snapshot generations are the subject of this conformance case.
    if (!tail.empty()) { if (error) *error = "UNEXPECTED_TAIL"; return nullptr; }
    return staged;
  }
};
void cleanup(const std::filesystem::path& path) {
  for (const auto& suffix : {"", ".bak", ".tmp", ".bak.tmp", ".compact.tmp", ".lock"})
    { std::error_code ec; std::filesystem::remove(path.string() + suffix, ec); }
}
}

int main() {
  using namespace surge;
  const auto root = std::filesystem::temp_directory_path();
  const auto journal_path = root / "surge_backup_retention.journal";
  const auto snapshot_path = root / "surge_backup_retention.snapshot";
  cleanup(journal_path); cleanup(snapshot_path);

  SnapshotStore store(snapshot_path.u8string());
  IntDomain domain;
  domain.value = 7;
  SnapshotCoordinator coordinator;
  std::string error;
  CHECK(coordinator.register_domain(&domain, &error));
  {
    EventJournal journal(journal_path.u8string());
    CHECK(journal.open());
    CHECK(!prune_verified_journal_backup(journal_path.u8string(), journal, store, &error));
    CHECK(error == "JOURNAL_BACKUP_PRUNE_REQUIRES_COMPACTED_JOURNAL");

    CHECK(journal.commit("tx-1", "idem-1", 0,
        {{"event-1", "NOOP", ""}}).committed);
    CHECK(coordinator.compact(journal, store, "retention-context", &error));
    CHECK(std::filesystem::exists(journal_path.string() + ".bak"));
    CHECK(std::filesystem::exists(snapshot_path.string() + ".bak"));
    CHECK(prune_verified_journal_backup(journal_path.u8string(), journal, store, &error));
    CHECK(!std::filesystem::exists(journal_path.string() + ".bak"));
    CHECK(std::filesystem::exists(snapshot_path.string() + ".bak"));
  }
  {
    EventJournal reopened(journal_path.u8string());
    CHECK(reopened.open());
    IntDomain restored;
    restored.value = -1;
    SnapshotCoordinator restore;
    CHECK(restore.register_domain(&restored, &error));
    CHECK(restore.restore_from_store(store, reopened, "retention-context",
        0, 0, JournalTailLimits{}, &error));
    CHECK(restored.value == 7);
  }
  cleanup(journal_path); cleanup(snapshot_path);
  std::cout << "SURGE verified journal backup retention tests passed\n";
  return 0;
}
