#include "test_support.hpp"
#include "surge/event_journal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using namespace surge;
int main() {
  const auto root = std::filesystem::temp_directory_path() / "surge-v09-journal-tests";
  std::filesystem::remove_all(root);
  const auto path = (root / "events.sgj").string();
  {
    EventJournal journal(path);
    CHECK(journal.open());
    auto a = journal.commit("tx-1", "idem-1", 0, {
      {"ev-1", "CLAIM_ADDED", "claim with spaces and | delimiter"},
      {"ev-2", "RELATION_ADDED", "line one\nline two"}
    });
    CHECK(a.committed && !a.duplicate && a.state_version == 1);
    auto retry = journal.commit("tx-1", "idem-1", 0, {
      {"ev-1", "CLAIM_ADDED", "claim with spaces and | delimiter"},
      {"ev-2", "RELATION_ADDED", "line one\nline two"}
    });
    CHECK(retry.committed && retry.duplicate && retry.state_version == 1);
    auto conflict = journal.commit("tx-2", "idem-1", 1, {{"ev-3","X","y"}});
    CHECK(conflict.error == "IDEMPOTENCY_KEY_REUSED");
    auto stale = journal.commit("tx-2", "idem-2", 0, {{"ev-3","X","y"}});
    CHECK(stale.error == "STATE_VERSION_CONFLICT");
    auto duplicate_id = journal.commit("tx-2", "idem-2", 1, {{"ev-1","X","y"}});
    CHECK(duplicate_id.error == "EVENT_ID_INVALID_OR_DUPLICATE");
    auto b = journal.commit("tx-2", "idem-2", 1, {{"ev-3","QUESTION_OPENED","why?"}});
    CHECK(b.committed && b.state_version == 2);
  }
  {
    EventJournal recovered(path);
    CHECK(recovered.open());
    CHECK(recovered.state_version() == 2);
    const auto events = recovered.committed_events();
    CHECK(events.size() == 3);
    CHECK(events[0].payload == "claim with spaces and | delimiter");
    CHECK(events[1].payload == "line one\nline two");
    auto retry = recovered.commit("tx-1", "idem-1", 0, {
      {"ev-1", "CLAIM_ADDED", "claim with spaces and | delimiter"},
      {"ev-2", "RELATION_ADDED", "line one\nline two"}
    });
    CHECK(retry.committed && retry.duplicate);
  }
  {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out.put(static_cast<char>(16)); out.put(static_cast<char>(0)); // deliberately truncated frame length
  }
  {
    EventJournal recovered(path);
    CHECK(recovered.open());
    CHECK(recovered.state_version() == 2);
    CHECK(recovered.committed_events().size() == 3);
    auto after_tail = recovered.commit("tx-3", "idem-3", 2, {{"ev-4","CLUSTER_UPDATED","post recovery append"}});
    CHECK(after_tail.committed && after_tail.state_version == 3);
  }
  {
    EventJournal recovered(path);
    CHECK(recovered.open());
    CHECK(recovered.state_version() == 3);
    CHECK(recovered.committed_events().size() == 4);
  }
  const auto corrupt_path = (root / "corrupt.sgj").string();
  {
    EventJournal seed(corrupt_path);
    CHECK(seed.open());
    auto seeded = seed.commit("tx-corrupt", "idem-corrupt", 0, {{"ev-corrupt","TEST","checksum"}});
    CHECK(seeded.committed);
  }
  CHECK(std::filesystem::exists(corrupt_path));
  {
    std::fstream io(corrupt_path, std::ios::binary | std::ios::in | std::ios::out);
    io.seekg(12); char byte = 0; io.read(&byte, 1); byte ^= 0x01;
    io.seekp(12); io.write(&byte, 1);
  }
  {
    EventJournal corrupt(corrupt_path);
    CHECK(!corrupt.open());
    CHECK(corrupt.last_error().find("JOURNAL_CHECKSUM_MISMATCH") == 0);
  }
  // Inject failure before each transaction frame: BEGIN, EVENT 1, EVENT 2, COMMIT.
  // Recovery must discard every incomplete transaction and permit an exact retry.
  for (std::size_t failure_offset = 0; failure_offset < 4; ++failure_offset) {
    const auto fault_path = (root / ("fault-" + std::to_string(failure_offset) + ".sgj")).string();
    std::size_t fail_at = static_cast<std::size_t>(-1);
    {
      EventJournal journal(fault_path, [&](std::size_t frame_index) {
        return frame_index != fail_at;
      });
      CHECK(journal.open());
      const auto seed = journal.commit("tx-seed", "idem-seed", 0,
          {{"ev-seed", "SEED", "committed baseline"}});
      CHECK(seed.committed && seed.state_version == 1);
      fail_at = 3 + failure_offset;
      const auto failed = journal.commit("tx-fault", "idem-fault", 1, {
          {"ev-fault-1", "FAULT_TEST", "first event"},
          {"ev-fault-2", "FAULT_TEST", "second event"}});
      CHECK(!failed.committed);
      CHECK(failed.error == "JOURNAL_INJECTED_APPEND_FAILURE");
      CHECK(journal.state_version() == 1);
      CHECK(journal.committed_events().size() == 1);
    }
    {
      EventJournal recovered(fault_path);
      CHECK(recovered.open());
      CHECK(recovered.state_version() == 1);
      CHECK(recovered.committed_events().size() == 1);
      const auto retry = recovered.commit("tx-fault", "idem-fault", 1, {
          {"ev-fault-1", "FAULT_TEST", "first event"},
          {"ev-fault-2", "FAULT_TEST", "second event"}});
      CHECK(retry.committed && !retry.duplicate && retry.state_version == 2);
    }
    {
      EventJournal recovered(fault_path);
      CHECK(recovered.open());
      CHECK(recovered.state_version() == 2);
      CHECK(recovered.committed_events().size() == 3);
    }
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE EventJournal tests: 6 groups passed\n";
}
