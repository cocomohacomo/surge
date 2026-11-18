#include "surge/snapshot_coordinator.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)

namespace surge_test {
class CounterDomain;
class CounterRestore final : public surge::PreparedSnapshotRestore {
 public:
  CounterRestore(CounterDomain* owner, int value) : owner_(owner), value_(value) {}
  void commit() noexcept override;
 private:
  CounterDomain* owner_;
  int value_;
};
class CounterDomain final : public surge::SnapshotDomain {
 public:
  int value{0};
  std::string domain_id() const override { return "counter"; }
  std::string schema_id() const override { return "surge.test.counter.v1"; }
  bool capture(std::string* payload, std::string* error) const override {
    if (!payload) { if (error) *error = "COUNTER_OUTPUT_REQUIRED"; return false; }
    *payload = std::to_string(value);
    if (error) error->clear();
    return true;
  }
  std::unique_ptr<surge::PreparedSnapshotRestore> prepare_restore(
      const std::string& payload, std::string* error) override {
    return prepare_restore_with_tail(payload, {}, error);
  }
  std::unique_ptr<surge::PreparedSnapshotRestore> prepare_restore_with_tail(
      const std::string& payload, const std::vector<surge::JournalEvent>& tail,
      std::string* error) override {
    int candidate = 0;
    std::istringstream input(payload);
    char extra = 0;
    if (!(input >> candidate) || (input >> extra)) {
      if (error) *error = "COUNTER_SNAPSHOT_INVALID";
      return nullptr;
    }
    for (const auto& event : tail) {
      if (event.kind != "COUNTER_ADD") continue;
      int delta = 0;
      std::istringstream value_stream(event.payload);
      if (!(value_stream >> delta) || (value_stream >> extra)) {
        if (error) *error = "COUNTER_TAIL_INVALID";
        return nullptr;
      }
      candidate += delta;
    }
    if (error) error->clear();
    return std::make_unique<CounterRestore>(this, candidate);
  }
};
void CounterRestore::commit() noexcept { owner_->value = value_; }
}

namespace {
void remove_path(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
}
void remove_generations(const std::filesystem::path& path) {
  remove_path(path);
  remove_path(path.string() + ".bak");
  remove_path(path.string() + ".tmp");
  remove_path(path.string() + ".bak.tmp");
  remove_path(path.string() + ".compact.tmp");
  remove_path(path.string() + ".lock");
}
}

int main() {
  using namespace surge;
  const auto root = std::filesystem::temp_directory_path();
  const auto journal_path = root / "surge_cow_compaction.journal";
  const auto snapshot_path = root / "surge_cow_compaction.snapshot";
  remove_generations(journal_path);
  remove_generations(snapshot_path);

  SnapshotStore store(snapshot_path.u8string());
  std::string first_checkpoint;
  std::uint64_t compacted_version = 0;
  std::uintmax_t journal_before = 0;
  std::uintmax_t journal_after = 0;

  {
    EventJournal journal(journal_path.u8string());
    CHECK(journal.open());
    surge_test::CounterDomain counter;
    SnapshotCoordinator coordinator;
    std::string error;
    CHECK(coordinator.register_domain(&counter, &error));

    auto append_delta = [&](int index, int delta) {
      const auto tx = "tx-" + std::to_string(index);
      const auto idem = "idem-" + std::to_string(index);
      const auto event = "event-" + std::to_string(index);
      const auto padding_event = "pad-" + std::to_string(index);
      const std::vector<JournalEvent> events = {
          {event, "COUNTER_ADD", std::to_string(delta)},
          {padding_event, "PADDING", std::string(2048, 'p')}};
      const auto committed = journal.commit(tx, idem, journal.state_version(), events);
      CHECK(committed.committed);
      counter.value += delta;
    };

    for (int i = 1; i <= 10; ++i) append_delta(i, 1);
    CHECK(coordinator.capture("counter-context", journal.state_version(),
        journal.state_version(), &first_checkpoint, &error));
    CHECK(store.save(first_checkpoint, &error));

    for (int i = 11; i <= 20; ++i) append_delta(i, 1);
    CHECK(counter.value == 20);
    compacted_version = journal.state_version();
    journal_before = std::filesystem::file_size(journal_path);

    CHECK(coordinator.compact(journal, store, "counter-context", &error));
    const auto checkpoint = journal.checkpoint_info();
    CHECK(checkpoint.compacted);
    CHECK(checkpoint.base_state_version == compacted_version);
    CHECK(checkpoint.context_id == "counter-context");
    CHECK(checkpoint.snapshot_digest.size() == 64);
    CHECK(journal.committed_events().empty());
    CHECK(journal.committed_tail(compacted_version - 1, {}).error ==
          "JOURNAL_TAIL_BEHIND_COMPACTION_BASE");
    CHECK(std::filesystem::exists(journal_path.string() + ".bak"));
    journal_after = std::filesystem::file_size(journal_path);
    CHECK(journal_after < journal_before);

    append_delta(21, 5);
    CHECK(journal.state_version() == compacted_version + 1);
    const auto tail = journal.committed_tail(compacted_version, {});
    CHECK(tail.ok && tail.events.size() == 2);
  }

  {
    EventJournal recovered(journal_path.u8string());
    CHECK(recovered.open());
    CHECK(recovered.checkpoint_info().compacted);
    CHECK(recovered.state_version() == compacted_version + 1);
    CHECK(recovered.committed_events().size() == 2);

    surge_test::CounterDomain counter;
    counter.value = -999;
    SnapshotCoordinator coordinator;
    std::string error;
    CHECK(coordinator.register_domain(&counter, &error));
    CHECK(coordinator.restore_from_store(store, recovered, "counter-context",
        0, 0, JournalTailLimits{4096, 8U * 1024U * 1024U}, &error));
    CHECK(counter.value == 25);

    const std::vector<JournalEvent> original_events = {
        {"event-1", "COUNTER_ADD", "1"},
        {"pad-1", "PADDING", std::string(2048, 'p')}};
    const auto duplicate = recovered.commit("tx-1", "idem-1", 0, original_events);
    CHECK(duplicate.committed && duplicate.duplicate);
    CHECK(recovered.state_version() == compacted_version + 1);

    const auto reused_tx = recovered.commit("tx-1", "idem-new",
        recovered.state_version(), {{"new-event", "COUNTER_ADD", "1"}});
    CHECK(!reused_tx.committed && reused_tx.error == "TRANSACTION_ID_REUSED");
    const auto reused_event = recovered.commit("tx-new", "idem-new-event",
        recovered.state_version(), {{"event-1", "COUNTER_ADD", "1"}});
    CHECK(!reused_event.committed);

    // The mirrored snapshot backup is the same checkpoint and can recover a
    // damaged primary while the compacted journal continues to hold its tail.
    {
      std::ofstream corrupt(snapshot_path, std::ios::binary | std::ios::trunc);
      CHECK(static_cast<bool>(corrupt));
      corrupt << "damaged-primary-snapshot";
    }
    const auto fallback = store.load();
    CHECK(fallback.ok && fallback.used_backup);
    CHECK(fallback.document.state_version == compacted_version);
    CHECK(coordinator.restore_from_store(store, recovered, "counter-context",
        0, 0, JournalTailLimits{4096, 8U * 1024U * 1024U}, &error));
    CHECK(counter.value == 25);
  }

  // The old journal generation remains a complete replay source at the
  // checkpoint watermark, independently of the compacted current journal.
  {
    EventJournal backup((journal_path.string() + ".bak"));
    CHECK(backup.open());
    CHECK(!backup.checkpoint_info().compacted);
    CHECK(backup.state_version() == compacted_version);
    CHECK(backup.committed_events().size() == 40);
  }

  remove_generations(journal_path);
  remove_generations(snapshot_path);
  std::cout << "SURGE copy-on-write journal compaction tests passed\n";
  return 0;
}
