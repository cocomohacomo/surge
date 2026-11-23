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
using SnapshotPayloadSink = std::function<bool(const char*, std::size_t, std::string*)>;
using SnapshotStoreStageGate = std::function<bool(const char*)>;

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
  explicit SnapshotStore(std::string path, SnapshotStoreLimits limits = {},
                         SnapshotStoreStageGate stage_gate = {});
  bool save(const std::string& bytes, std::string* error);
  // Reads payload chunks (1..64 KiB, done=false), followed by one final
  // empty chunk (done=true). Writes a version-1 envelope without buffering
  // the full payload or envelope. The reader must not throw.
  bool save_stream(const SnapshotDocument& metadata, std::size_t payload_bytes,
                   const SnapshotChunkReader& reader, std::string* error);
  // Makes the backup an exact durable copy of the current generation. Used
  // before journal replacement so both recovery generations share a watermark.
  bool mirror_current_to_backup(std::string* error);
  SnapshotStoreResult load() const;
  // Verify the complete generation in a bounded first pass, then deliver the
  // authenticated payload in <=64 KiB chunks. The sink is never called for an
  // invalid checksum. Primary corruption falls back to the verified backup.
  bool load_stream(SnapshotDocument* metadata, const SnapshotPayloadSink& sink,
                   bool* used_backup, std::string* error) const;

  const std::string& path() const { return path_; }
  std::size_t max_file_bytes() const { return limits_.max_file_bytes; }
  std::string backup_path() const { return path_ + ".bak"; }

  // Test-only deterministic failure injection at durable replacement stages.
  bool permit_stage(const char* stage, std::string* error) const;

 private:
  std::string path_;
  SnapshotStoreLimits limits_;
  SnapshotStoreStageGate stage_gate_;
  mutable std::mutex mutex_;
};

}  // namespace surge
