#include "surge/event_journal.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)
int main() {
  using namespace surge;
  const auto root = std::filesystem::temp_directory_path() / "surge-journal-tail-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto path = (root / "events.sgj").string();
  {
    EventJournal journal(path);
    CHECK(journal.open());
    auto first = journal.commit("tx-1", "idem-1", 0,
        {{"event-1a", "KIND_A", "alpha"}, {"event-1b", "KIND_B", "beta"}});
    CHECK(first.committed && first.state_version == 1);
    auto second = journal.commit("tx-2", "idem-2", 1,
        {{"event-2", "KIND_C", "gamma"}});
    CHECK(second.committed && second.state_version == 2);

    const auto all = journal.committed_tail(0);
    CHECK(all.ok && all.after_state_version == 0 && all.current_state_version == 2);
    CHECK(all.events.size() == 3 && all.payload_bytes == 14);
    CHECK(all.events[0].state_version == 1 && all.events[1].state_version == 1);
    CHECK(all.events[2].state_version == 2);

    const auto tail = journal.committed_tail(1);
    CHECK(tail.ok && tail.events.size() == 1);
    CHECK(tail.events[0].event_id == "event-2" && tail.events[0].state_version == 2);
    const auto empty = journal.committed_tail(2);
    CHECK(empty.ok && empty.events.empty());
    CHECK(journal.committed_tail(3).error == "JOURNAL_TAIL_WATERMARK_AHEAD");

    JournalTailLimits event_limit;
    event_limit.max_events = 1;
    CHECK(journal.committed_tail(0, event_limit).error ==
          "JOURNAL_TAIL_EVENT_LIMIT_EXCEEDED");
    JournalTailLimits byte_limit;
    byte_limit.max_payload_bytes = 4;
    CHECK(journal.committed_tail(0, byte_limit).error ==
          "JOURNAL_TAIL_PAYLOAD_LIMIT_EXCEEDED");
  }
  {
    EventJournal recovered(path);
    CHECK(recovered.open());
    const auto tail = recovered.committed_tail(1);
    CHECK(tail.ok && tail.events.size() == 1);
    CHECK(tail.events.front().state_version == 2);
    CHECK(tail.events.front().event_id == "event-2");
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE journal tail watermark tests passed\n";
  return 0;
}
