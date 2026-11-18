#include "surge/journal_backup_retention.hpp"
#include "surge/sha256.hpp"

#include <filesystem>
#include <string>
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
namespace fs = std::filesystem;
bool fail(std::string* error, const char* value) {
  if (error) *error = value;
  return false;
}
bool sync_directory(const fs::path& path) {
#ifdef _WIN32
  (void)path;
  return true; // Win32 has no portable directory FlushFileBuffers contract.
#else
  auto parent = path.parent_path();
  if (parent.empty()) parent = ".";
  const int fd = ::open(parent.c_str(), O_RDONLY
#ifdef O_DIRECTORY
      | O_DIRECTORY
#endif
  );
  if (fd < 0) return false;
  const bool ok = ::fsync(fd) == 0;
  const bool closed = ::close(fd) == 0;
  return ok && closed;
#endif
}
bool matches_checkpoint(const SnapshotStoreResult& snapshot,
                        const JournalCheckpointInfo& checkpoint) {
  if (!snapshot.ok || snapshot.document.context_id != checkpoint.context_id ||
      snapshot.document.state_version != snapshot.document.journal_sequence ||
      snapshot.document.state_version < checkpoint.base_state_version)
    return false;
  return snapshot.document.state_version != checkpoint.base_state_version ||
         sha256_hex(snapshot.bytes) == checkpoint.snapshot_digest;
}
bool is_exact_checkpoint(const SnapshotStoreResult& snapshot,
                         const JournalCheckpointInfo& checkpoint) {
  return snapshot.ok &&
      snapshot.document.context_id == checkpoint.context_id &&
      snapshot.document.state_version == checkpoint.base_state_version &&
      snapshot.document.journal_sequence == checkpoint.base_state_version &&
      sha256_hex(snapshot.bytes) == checkpoint.snapshot_digest;
}
}  // namespace

bool prune_verified_journal_backup(const std::string& journal_path,
    EventJournal& active_journal, SnapshotStore& snapshot_store,
    std::string* error) {
  if (journal_path.empty()) return fail(error, "JOURNAL_BACKUP_PATH_REQUIRED");
  const auto requested_path = fs::absolute(fs::u8path(journal_path)).lexically_normal();
  const auto active_path = fs::absolute(fs::u8path(active_journal.path())).lexically_normal();
  if (requested_path != active_path)
    return fail(error, "JOURNAL_BACKUP_PRUNE_ACTIVE_PATH_MISMATCH");
  const auto checkpoint = active_journal.checkpoint_info();
  if (!checkpoint.compacted || checkpoint.context_id.empty() ||
      checkpoint.snapshot_digest.size() != 64)
    return fail(error, "JOURNAL_BACKUP_PRUNE_REQUIRES_COMPACTED_JOURNAL");

  const auto current_snapshot = snapshot_store.load();
  if (!matches_checkpoint(current_snapshot, checkpoint))
    return fail(error, "JOURNAL_BACKUP_PRUNE_CURRENT_SNAPSHOT_MISMATCH");

  // Require a separately readable backup generation. A valid primary alone
  // is insufficient because pruning must preserve recovery after primary loss.
  SnapshotStore backup_store(snapshot_store.backup_path(),
      SnapshotStoreLimits{snapshot_store.max_file_bytes(), false});
  const auto backup_snapshot = backup_store.load();
  if (!matches_checkpoint(backup_snapshot, checkpoint))
    return fail(error, "JOURNAL_BACKUP_PRUNE_BACKUP_SNAPSHOT_MISMATCH");
  if (!is_exact_checkpoint(current_snapshot, checkpoint) &&
      !is_exact_checkpoint(backup_snapshot, checkpoint))
    return fail(error, "JOURNAL_BACKUP_PRUNE_EXACT_CHECKPOINT_COPY_REQUIRED");

  const auto backup_path = fs::u8path(journal_path + ".bak");
  std::error_code ec;
  if (!fs::exists(backup_path, ec)) {
    if (ec) return fail(error, "JOURNAL_BACKUP_PRUNE_STAT_FAILED");
    if (error) error->clear();
    return true;
  }
  if (!fs::is_regular_file(backup_path, ec) || ec)
    return fail(error, "JOURNAL_BACKUP_PRUNE_NOT_REGULAR_FILE");

  // Verify that the retained generation is the expected complete pre-COW
  // journal before deleting it. It must reach exactly the compacted watermark.
  {
    EventJournal retained(backup_path.u8string());
    if (!retained.open())
      return fail(error, "JOURNAL_BACKUP_PRUNE_RETAINED_JOURNAL_INVALID");
    const auto info = retained.checkpoint_info();
    if (info.compacted || retained.state_version() != checkpoint.base_state_version)
      return fail(error, "JOURNAL_BACKUP_PRUNE_RETAINED_WATERMARK_MISMATCH");
  }

  if (!fs::remove(backup_path, ec) || ec)
    return fail(error, "JOURNAL_BACKUP_PRUNE_REMOVE_FAILED");
  if (!sync_directory(backup_path))
    return fail(error, "JOURNAL_BACKUP_PRUNE_DIRECTORY_SYNC_FAILED");
  if (error) error->clear();
  return true;
}
}  // namespace surge
