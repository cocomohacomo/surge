#include "surge/snapshot_coordinator.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)

namespace {
class ValueDomain final : public surge::SnapshotDomain {
 public:
  int value{0};
  std::string domain_id() const override { return "value"; }
  std::string schema_id() const override { return "surge.test.value.v1"; }
  bool capture(std::string* payload, std::string* error) const override {
    if (!payload) { if (error) *error = "OUTPUT_REQUIRED"; return false; }
    *payload = std::to_string(value);
    if (error) error->clear();
    return true;
  }
  std::unique_ptr<surge::PreparedSnapshotRestore> prepare_restore(
      const std::string&, std::string* error) override {
    if (error) *error = "NOT_USED";
    return nullptr;
  }
};
void remove_generation(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path.string() + ".bak", ec);
  std::filesystem::remove(path.string() + ".tmp", ec);
  std::filesystem::remove(path.string() + ".bak.tmp", ec);
  std::filesystem::remove(path.string() + ".compact.tmp", ec);
  std::filesystem::remove(path.string() + ".lock", ec);
}
}

int main() {
  using namespace surge;
  const auto root = std::filesystem::temp_directory_path();
  const auto journal_path = root / "surge_backup_retention.journal";
  const auto snapshot_path = root / "surge_backup_retention.snapshot";
  const auto wrong_snapshot_path = root / "surge_backup_retention_wrong.snapshot";
  remove_generation(journal_path);
  remove_generation(snapshot_path);
  remove_generation(wrong_snapshot_path);

  SnapshotStore store(snapshot_path.u8string());
  SnapshotStore wrong_store(wrong_snapshot_path.u8string());
  EventJournal journal(journal_path.u8string());
  CHECK(journal.open());

  ValueDomain domain;
  SnapshotCoordinator coordinator;
  std::string error;
  CHECK(coordinator.register_domain(&domain, &error));
  CHECK(journal.commit("tx-1", "idem-1", 0,
      {{"event-1", "VALUE_SET", "7"}}).committed);
  domain.value = 7;
  CHECK(coordinator.compact(journal, store, "retention-context", &error));
  CHECK(std::filesystem::exists(journal_path.string() + ".bak"));

  SnapshotDocument divergent;
  divergent.schema_id = "surge.snapshot.bundle.v1";
  divergent.context_id = "retention-context";
  divergent.state_version = journal.checkpoint_info().base_state_version;
  divergent.journal_sequence = divergent.state_version;
  divergent.payload = "different-but-valid-checkpoint";
  std::string wrong_bytes;
  CHECK(SnapshotCodec::encode(divergent, {}, &wrong_bytes, &error));
  CHECK(wrong_store.save(wrong_bytes, &error));

  // A valid but divergent checkpoint must never authorize deletion.
  CHECK(!journal.prune_backup_after_checkpoint(wrong_store, &error));
  CHECK(error == "JOURNAL_BACKUP_PRUNE_CHECKPOINT_MISMATCH");
  CHECK(std::filesystem::exists(journal_path.string() + ".bak"));

  // The exact checkpoint bound into the compacted journal authorizes pruning.
  CHECK(journal.prune_backup_after_checkpoint(store, &error));
  CHECK(!std::filesystem::exists(journal_path.string() + ".bak"));
  CHECK(journal.prune_backup_after_checkpoint(store, &error)); // idempotent

  // Current compacted journal plus the exact snapshot remain a valid restore
  // anchor after the old full journal generation has been explicitly pruned.
  ValueDomain restored;
  SnapshotCoordinator restore;
  CHECK(restore.register_domain(&restored, &error));
  CHECK(restore.restore_from_store(store, journal, "retention-context",
      0, 0, JournalTailLimits{}, &error));
  CHECK(restored.value == 7);

  remove_generation(journal_path);
  remove_generation(snapshot_path);
  remove_generation(wrong_snapshot_path);
  std::cout << "SURGE journal backup retention tests passed\n";
  return 0;
}
