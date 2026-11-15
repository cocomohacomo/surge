#include "surge/event_journal.hpp"
#include <cassert>
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
    assert(journal.open());
    auto a = journal.commit("tx-1", "idem-1", 0, {
      {"ev-1", "CLAIM_ADDED", "claim with spaces and | delimiter"},
      {"ev-2", "RELATION_ADDED", "line one\nline two"}
    });
    assert(a.committed && !a.duplicate && a.state_version == 1);
    auto retry = journal.commit("tx-1", "idem-1", 0, {
      {"ev-1", "CLAIM_ADDED", "claim with spaces and | delimiter"},
      {"ev-2", "RELATION_ADDED", "line one\nline two"}
    });
    assert(retry.committed && retry.duplicate && retry.state_version == 1);
    auto conflict = journal.commit("tx-2", "idem-1", 1, {{"ev-3","X","y"}});
    assert(conflict.error == "IDEMPOTENCY_KEY_REUSED");
    auto stale = journal.commit("tx-2", "idem-2", 0, {{"ev-3","X","y"}});
    assert(stale.error == "STATE_VERSION_CONFLICT");
    auto duplicate_id = journal.commit("tx-2", "idem-2", 1, {{"ev-1","X","y"}});
    assert(duplicate_id.error == "EVENT_ID_INVALID_OR_DUPLICATE");
    auto b = journal.commit("tx-2", "idem-2", 1, {{"ev-3","QUESTION_OPENED","why?"}});
    assert(b.committed && b.state_version == 2);
  }
  {
    EventJournal recovered(path);
    assert(recovered.open());
    assert(recovered.state_version() == 2);
    const auto events = recovered.committed_events();
    assert(events.size() == 3);
    assert(events[0].payload == "claim with spaces and | delimiter");
    assert(events[1].payload == "line one\nline two");
    auto retry = recovered.commit("tx-1", "idem-1", 0, {
      {"ev-1", "CLAIM_ADDED", "claim with spaces and | delimiter"},
      {"ev-2", "RELATION_ADDED", "line one\nline two"}
    });
    assert(retry.committed && retry.duplicate);
  }
  {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out.put(static_cast<char>(16)); out.put(static_cast<char>(0)); // deliberately truncated frame length
  }
  {
    EventJournal recovered(path);
    assert(recovered.open());
    assert(recovered.state_version() == 2);
    assert(recovered.committed_events().size() == 3);
    auto after_tail = recovered.commit("tx-3", "idem-3", 2, {{"ev-4","CLUSTER_UPDATED","post recovery append"}});
    assert(after_tail.committed && after_tail.state_version == 3);
  }
  {
    EventJournal recovered(path);
    assert(recovered.open());
    assert(recovered.state_version() == 3);
    assert(recovered.committed_events().size() == 4);
  }
  const auto corrupt_path = (root / "corrupt.sgj").string();
  {
    EventJournal seed(corrupt_path);
    assert(seed.open());
    auto seeded = seed.commit("tx-corrupt", "idem-corrupt", 0, {{"ev-corrupt","TEST","checksum"}});
    assert(seeded.committed);
  }
  assert(std::filesystem::exists(corrupt_path));
  {
    std::fstream io(corrupt_path, std::ios::binary | std::ios::in | std::ios::out);
    io.seekg(12); char byte = 0; io.read(&byte, 1); byte ^= 0x01;
    io.seekp(12); io.write(&byte, 1);
  }
  {
    EventJournal corrupt(corrupt_path);
    assert(!corrupt.open());
    assert(corrupt.last_error().find("JOURNAL_CHECKSUM_MISMATCH") == 0);
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE EventJournal tests: 5 groups passed\n";
}
