#include "surge/event_journal.hpp"
#include "surge/snapshot_store.hpp"
#include "surge/sha256.hpp"

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace surge {
namespace {
bool sync_journal_parent(const std::string& path) {
#ifdef _WIN32
  (void)path;
  // Windows replacement/deletion APIs do not expose a portable directory
  // FlushFileBuffers operation. File deletion is still checked by the caller.
  return true;
#else
  auto parent = std::filesystem::path(path).parent_path();
  if (parent.empty()) parent = ".";
  const int fd = ::open(parent.c_str(), O_RDONLY
#ifdef O_DIRECTORY
      | O_DIRECTORY
#endif
  );
  if (fd < 0) return false;
  const bool synced = ::fsync(fd) == 0;
  const bool closed = ::close(fd) == 0;
  return synced && closed;
#endif
}
}  // namespace

bool EventJournal::prune_backup_after_checkpoint(
    const SnapshotStore& store, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto fail = [error](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!opened_) return fail("JOURNAL_NOT_OPEN");
  if (!compacted_ || checkpoint_context_id_.empty() ||
      checkpoint_snapshot_digest_.size() != 64)
    return fail("JOURNAL_BACKUP_PRUNE_REQUIRES_COMPACTION");

  // Only an exact, independently validated checkpoint can replace the old
  // full journal as the recovery anchor. A later snapshot is not accepted:
  // the digest must bind this exact compaction generation.
  const auto checkpoint = store.load();
  if (!checkpoint.ok)
    return fail("JOURNAL_BACKUP_PRUNE_SNAPSHOT_UNAVAILABLE");
  if (checkpoint.document.context_id != checkpoint_context_id_ ||
      checkpoint.document.state_version != checkpoint_base_state_version_ ||
      checkpoint.document.journal_sequence != checkpoint_base_state_version_ ||
      sha256_hex(checkpoint.bytes) != checkpoint_snapshot_digest_)
    return fail("JOURNAL_BACKUP_PRUNE_CHECKPOINT_MISMATCH");

  const auto backup = std::filesystem::path(path_ + ".bak");
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(backup, ec);
  if (ec == std::errc::no_such_file_or_directory ||
      (!ec && !std::filesystem::exists(status))) {
    if (error) error->clear();
    return true;  // Idempotent: the redundant generation is already absent.
  }
  if (ec) return fail("JOURNAL_BACKUP_PRUNE_STAT_FAILED");
  if (!std::filesystem::is_regular_file(status))
    return fail("JOURNAL_BACKUP_PRUNE_NOT_REGULAR_FILE");

  if (!std::filesystem::remove(backup, ec) || ec)
    return fail("JOURNAL_BACKUP_PRUNE_REMOVE_FAILED");
  if (!sync_journal_parent(path_))
    return fail("JOURNAL_BACKUP_PRUNE_DIRECTORY_SYNC_FAILED");
  if (error) error->clear();
  return true;
}
}  // namespace surge
