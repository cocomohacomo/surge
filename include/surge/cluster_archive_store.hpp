#pragma once
#include "surge/snapshot_store.hpp"
#include <cstdint>
#include <string>
namespace surge {
struct ClusterArchiveRecord {
  std::string cluster_id;
  std::uint64_t state_version{0};
  std::uint64_t journal_sequence{0};
  std::string payload;
};
// Durable per-cluster payload archive. SnapshotStore supplies atomic
// generations, SHA-256 validation, and backup recovery. This component
// deliberately does not mutate ContextGraph.
class ClusterArchiveStore {
 public:
  explicit ClusterArchiveStore(std::string path, SnapshotStoreLimits limits = {});
  bool archive(const std::string& cluster_id, std::uint64_t state_version,
      std::uint64_t journal_sequence, const std::string& payload,
      std::string* error);
  bool restore(const std::string& expected_cluster_id,
      std::uint64_t minimum_state_version, ClusterArchiveRecord* output,
      std::string* error) const;
  const std::string& path() const noexcept { return store_.path(); }
 private:
  SnapshotStore store_;
};
}  // namespace surge
