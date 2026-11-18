#include "surge/event_journal.hpp"

#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: surge_journal_verify JOURNAL_PATH\n";
    return 64;
  }

  surge::EventJournal journal(argv[1]);
  if (!journal.open()) {
    std::cerr << journal.last_error() << "\n";
    return 2;
  }

  const auto events = journal.committed_events();
  std::cout << "{\"state_version\":" << journal.state_version()
            << ",\"event_count\":" << events.size() << "}\n";
  return 0;
}
