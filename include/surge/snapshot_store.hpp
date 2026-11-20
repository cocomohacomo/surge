#pragma once
#include "surge/snapshot.hpp"
#include <functional>
#include <cstddef>
#include <mutex>
#include <string>

namespace surge {

struct SnapshotStoreLimits {
  std::size_t max_file_bytes{64U * 1024U * 1024U};
  bool keep_backup{true};
};

using SnapshotChunkReader = std::function<bool(std::string*, bool*)>;

struct SnapshotStoreResult {
  // document contains envelope metadata only; the payload remains in bytes.
  bool ok{false};
  bool used_backup{false};
  // Metadata only; payload remains in bytes to avoid duplicating large snapshots.
  SnapshotDocument document;
  std::string bytes;
  std::string error;
};

// Durable, bounded snapshot generations. Save writes and syncs a same-directory
// temporary file before atomic replacement. A previous generation is retained
// as ".bak" when enabled. One writer per path is required.
class SnapshotStore {
 public:
  explicit SnapshotStore(std::string path, SnapshotStoreLimits limits = {});
  bool save(const std::string& bytes, std::string* error);
  // Reads payload chunks (at most 64 KiB each) and writes a complete
  // version-1 envelope without materializing the payload or envelope.
  bool save_stream(const SnapshotDocument& metadata, std::size_t payload_bytes,
                   const SnapshotChunkReader& reader, std::string* error);
  // Makes the backup an exact durable copy of the current generation. Used
  // before journal replacement so both recovery generations share a watermark.
  bool mirror_current_to_backup(std::string* error);
  SnapshotStoreResult load() const;

  const std::string& path() const { return path_; }
  std::size_t max_file_bytes() const { return limits_.max_file_bytes; }
  std::string backup_path() const { return path_ + ".bak"; }

 private:
  std::string path_;
  SnapshotStoreLimits limits_;
  mutable std::mutex mutex_;
};

}  // namespace surge
