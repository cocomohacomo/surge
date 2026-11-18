#include "test_support.hpp"
#include "surge/event_journal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace surge;
int main(int argc, char** argv) {
  // Child mode used by the parent test to hold the OS writer lock in a
  // genuinely separate process until a release marker appears.
  if (argc == 4 && std::string(argv[1]) == "--hold-lock") {
    EventJournal holder(argv[2]);
    if (!holder.open()) return 2;
    { std::ofstream ready(argv[3]); ready << "ready\n"; }
    const std::filesystem::path release = std::string(argv[3]) + ".release";
    for (int i = 0; i < 500; ++i) {
      if (std::filesystem::exists(release)) return 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 3;
  }
  const auto root = std::filesystem::temp_directory_path() / "surge-v09-journal-tests";
  std::filesystem::remove_all(root);
  const auto lock_path = (root / "writer-lock.sgj").string();
  {
    EventJournal first(lock_path);
    CHECK(first.open());
    EventJournal competing(lock_path);
    CHECK(!competing.open());
    CHECK(competing.last_error() == "JOURNAL_WRITER_LOCKED");
  }
  {
    EventJournal after_release(lock_path);
    CHECK(after_release.open());
  }
  // Cross-process writer exclusion: a child holds the journal lock while
  // the parent attempts to open the same journal through another process.
  {
    const auto process_path = (root / "multiprocess.sgj").string();
    const auto ready_path = (root / "multiprocess.ready").string();
    const auto release_path = ready_path + ".release";
    const auto executable = std::filesystem::absolute(argv[0]).string();
#ifdef _WIN32
    STARTUPINFOA startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::string command = "\"" + executable + "\" --hold-lock \"" +
      process_path + "\" \"" + ready_path + "\"";
    std::vector<char> command_buffer(command.begin(), command.end());
    command_buffer.push_back('\0');
    CHECK(CreateProcessA(nullptr, command_buffer.data(), nullptr, nullptr, FALSE,
      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != 0);
#else
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
      execl(executable.c_str(), executable.c_str(), "--hold-lock",
        process_path.c_str(), ready_path.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
#endif
    bool ready = false;
    for (int i = 0; i < 500; ++i) {
      if (std::filesystem::exists(ready_path)) { ready = true; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(ready);
    {
      EventJournal competing_process(process_path);
      CHECK(!competing_process.open());
      CHECK(competing_process.last_error() == "JOURNAL_WRITER_LOCKED");
    }
    { std::ofstream release(release_path); release << "release\n"; }
#ifdef _WIN32
    CHECK(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD exit_code = 1; CHECK(GetExitCodeProcess(process.hProcess, &exit_code) != 0);
    CHECK(exit_code == 0);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
#else
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif
  }
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
  // A failed sync barrier before the COMMIT marker is definitely uncommitted:
  // replay discards the incomplete transaction and an exact retry can proceed.
  const auto precommit_sync_path=(root/"precommit-sync-failure.sgj").string();
  {
    EventJournal journal(precommit_sync_path, EventJournal::AppendFrameGate{},
      [](std::size_t frame_index) { return frame_index != 1; });
    CHECK(journal.open());
    const auto failed=journal.commit("tx-precommit-sync","idem-precommit-sync",0,
      {{"ev-precommit-sync","TEST","sync failed before commit marker"}});
    CHECK(!failed.committed);
    CHECK(failed.error=="JOURNAL_INJECTED_SYNC_FAILURE");
    CHECK(journal.state_version()==0);
  }
  {
    EventJournal recovered(precommit_sync_path);
    CHECK(recovered.open());
    CHECK(recovered.state_version()==0);
    CHECK(recovered.committed_events().empty());
    const auto retry=recovered.commit("tx-precommit-sync","idem-precommit-sync",0,
      {{"ev-precommit-sync","TEST","sync failed before commit marker"}});
    CHECK(retry.committed&&!retry.duplicate&&retry.state_version==1);
  }

  // A failed sync barrier after writing the COMMIT marker is ambiguous. On
  // reopen the marker is replayed if present; exact idempotent retry resolves
  // the result without duplicating the transaction.
  const auto commit_sync_path=(root/"commit-sync-failure.sgj").string();
  {
    EventJournal journal(commit_sync_path, EventJournal::AppendFrameGate{},
      [](std::size_t frame_index) { return frame_index != 2; });
    CHECK(journal.open());
    const auto failed=journal.commit("tx-commit-sync","idem-commit-sync",0,
      {{"ev-commit-sync","TEST","commit marker sync barrier failed"}});
    CHECK(!failed.committed);
    CHECK(failed.error=="JOURNAL_COMMIT_OUTCOME_UNKNOWN");
    CHECK(journal.state_version()==0);
  }
  {
    EventJournal recovered(commit_sync_path);
    CHECK(recovered.open());
    CHECK(recovered.state_version()==1);
    CHECK(recovered.committed_events().size()==1);
    const auto retry=recovered.commit("tx-commit-sync","idem-commit-sync",0,
      {{"ev-commit-sync","TEST","commit marker sync barrier failed"}});
    CHECK(retry.committed&&retry.duplicate&&retry.state_version==1);
  }
  // Model a power-loss outcome where the unsynchronized COMMIT marker is
  // absent after restart but the earlier BEGIN/EVENT frames had reached disk.
  const auto commit_sync_lost_path=(root/"commit-sync-marker-lost.sgj").string();
  std::uintmax_t size_after_event_frame=0;
  {
    EventJournal journal(commit_sync_lost_path, EventJournal::AppendFrameGate{},
      [&](std::size_t frame_index) {
        if (frame_index==1)
          size_after_event_frame=std::filesystem::file_size(commit_sync_lost_path);
        return frame_index!=2;
      });
    CHECK(journal.open());
    const auto failed=journal.commit("tx-commit-lost","idem-commit-lost",0,
      {{"ev-commit-lost","TEST","commit marker lost after sync failure"}});
    CHECK(!failed.committed);
    CHECK(failed.error=="JOURNAL_COMMIT_OUTCOME_UNKNOWN");
    CHECK(size_after_event_frame>0);
  }
  std::filesystem::resize_file(commit_sync_lost_path,size_after_event_frame);
  {
    EventJournal recovered(commit_sync_lost_path);
    CHECK(recovered.open());
    CHECK(recovered.state_version()==0);
    CHECK(recovered.committed_events().empty());
    const auto retry=recovered.commit("tx-commit-lost","idem-commit-lost",0,
      {{"ev-commit-lost","TEST","commit marker lost after sync failure"}});
    CHECK(retry.committed&&!retry.duplicate&&retry.state_version==1);
  }
  // Configurable memory budgets fail closed before durable append and on replay.
  const auto bounded_path = (root / "bounded.sgj").string();
  {
    EventJournalLimits limits;
    limits.max_events = 2;
    limits.max_payload_bytes = 12;
    limits.max_transactions = 2;
    EventJournal bounded(bounded_path, limits);
    CHECK(bounded.open());
    const auto first = bounded.commit("bounded-1", "bounded-idem-1", 0,
        {{"bounded-event-1", "TEST", "123456"}});
    CHECK(first.committed);
    const auto second = bounded.commit("bounded-2", "bounded-idem-2", 1,
        {{"bounded-event-2", "TEST", "abcdef"}});
    CHECK(second.committed);
    const auto rejected = bounded.commit("bounded-3", "bounded-idem-3", 2,
        {{"bounded-event-3", "TEST", "x"}});
    CHECK(!rejected.committed);
    CHECK(rejected.error == "JOURNAL_RESOURCE_LIMIT_EXCEEDED");
    CHECK(bounded.state_version() == 2);
  }
  {
    EventJournalLimits limits;
    limits.max_events = 1;
    EventJournal bounded_replay(bounded_path, limits);
    CHECK(!bounded_replay.open());
    CHECK(bounded_replay.last_error() == "JOURNAL_RESOURCE_LIMIT_EXCEEDED");
  }
  {
    EventJournalLimits limits;
    limits.max_payload_bytes = 3;
    EventJournal payload_limited((root / "payload-limited.sgj").string(), limits);
    CHECK(payload_limited.open());
    const auto rejected = payload_limited.commit("payload-tx", "payload-idem", 0,
        {{"payload-event", "TEST", "four"}});
    CHECK(!rejected.committed);
    CHECK(rejected.error == "JOURNAL_RESOURCE_LIMIT_EXCEEDED");
    CHECK(payload_limited.state_version() == 0);
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE EventJournal tests: sync uncertainty recovery passed\n";
}
