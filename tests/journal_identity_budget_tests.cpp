#include "surge/snapshot_coordinator.hpp"
#include <filesystem>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; return 1; } } while (0)

namespace {
class Restore final : public surge::PreparedSnapshotRestore {
 public:
  explicit Restore(int* target) : target_(target) {}
  void commit() noexcept override { *target_ = 9; }
 private:
  int* target_;
};
class Domain final : public surge::SnapshotDomain {
 public:
  int value{9};
  std::string domain_id() const override { return "identity-budget"; }
  std::string schema_id() const override { return "identity-budget.v1"; }
  bool capture(std::string* payload, std::string* error) const override {
    if (!payload) { if (error) *error = "OUTPUT_REQUIRED"; return false; }
    *payload = std::to_string(value);
    if (error) error->clear();
    return true;
  }
  std::unique_ptr<surge::PreparedSnapshotRestore> prepare_restore(
      const std::string& payload, std::string* error) override {
    if (payload != "9") { if (error) *error = "INVALID"; return nullptr; }
    if (error) error->clear();
    return std::make_unique<Restore>(&value);
  }
};
void cleanup(const std::filesystem::path& path) {
  for (const auto* suffix : {"", ".bak", ".tmp", ".bak.tmp", ".compact.tmp", ".lock"}) {
    std::error_code ec;
    std::filesystem::remove(path.string() + suffix, ec);
  }
}
}

int main() {
  using namespace surge;
  const auto root = std::filesystem::temp_directory_path();
  const auto path = root / "surge_identity_budget.journal";
  const auto snapshot_path = root / "surge_identity_budget.snapshot";
  cleanup(path); cleanup(snapshot_path);

  EventJournalLimits limits;
  limits.max_identity_bytes = 500;
  SnapshotStore store(snapshot_path.u8string());
  Domain domain;
  SnapshotCoordinator coordinator;
  std::string error;
  CHECK(coordinator.register_domain(&domain, &error));
  {
    EventJournal journal(path.u8string(), limits);
    CHECK(journal.open());
    const auto first = journal.commit("tx-a", "idem-a", 0,
        {{"event-a", "NOOP", "payload"}});
    CHECK(first.committed);
    CHECK(journal.identity_memory_bytes() == 401);
    const auto second = journal.commit("tx-b", "idem-b", 1,
        {{"event-b", "NOOP", "payload"}});
    CHECK(!second.committed && second.error == "JOURNAL_RESOURCE_LIMIT_EXCEEDED");
    CHECK(journal.state_version() == 1);
    CHECK(coordinator.compact(journal, store, "identity-budget-context", &error));
  }
  {
    EventJournalLimits too_small;
    too_small.max_identity_bytes = 400;
    EventJournal rejected(path.u8string(), too_small);
    CHECK(!rejected.open());
    CHECK(rejected.last_error() == "JOURNAL_CHECKPOINT_HEADER_INVALID");
  }
  {
    EventJournal restored(path.u8string(), limits);
    CHECK(restored.open());
    CHECK(restored.identity_memory_bytes() == 401);
    const auto duplicate = restored.commit("tx-a", "idem-a", 0,
        {{"event-a", "NOOP", "payload"}});
    CHECK(duplicate.committed && duplicate.duplicate);
  }
  cleanup(path); cleanup(snapshot_path);
  std::cout << "SURGE journal identity budget tests passed\n";
  return 0;
}
