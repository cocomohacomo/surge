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
    out.put('\x10'); out.put('\x00'); // deliberately truncated frame length
  }
  {
    EventJournal recovered(path);
    assert(recovered.open());
    assert(recovered.state_version() == 2);
    assert(recovered.committed_events().size() == 3);
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE EventJournal tests: 3 groups passed\n";
}
