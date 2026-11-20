#include "surge/cluster_archive_store.hpp"
#include <utility>
namespace surge {
namespace { constexpr const char* kSchema = "surge.cluster.archive.v1"; }
ClusterArchiveStore::ClusterArchiveStore(std::string path, SnapshotStoreLimits limits)
    : store_(std::move(path), limits) {}
bool ClusterArchiveStore::archive(const std::string& cluster_id,
    std::uint64_t state_version, std::uint64_t journal_sequence,
    const std::string& payload, std::string* error) {
  if (cluster_id.empty() || state_version == 0 || payload.empty()) {
    if (error) *error = "CLUSTER_ARCHIVE_INPUT_INVALID";
    return false;
  }
  SnapshotDocument document;
  document.schema_id = kSchema; document.context_id = cluster_id;
  document.state_version = state_version; document.journal_sequence = journal_sequence;
  document.payload = payload;
  std::string bytes;
  if (!SnapshotCodec::encode(document, {}, &bytes, error)) return false;
  return store_.save(bytes, error);
}
bool ClusterArchiveStore::restore(const std::string& expected_cluster_id,
    std::uint64_t minimum_state_version, ClusterArchiveRecord* output,
    std::string* error) const {
  if (!output || expected_cluster_id.empty()) {
    if (error) *error = "CLUSTER_ARCHIVE_RESTORE_ARGUMENT_INVALID";
    return false;
  }
  auto loaded = store_.load();
  if (!loaded.ok) {
    if (error) *error = loaded.error.empty() ? "CLUSTER_ARCHIVE_GENERATION_UNAVAILABLE" : loaded.error;
    return false;
  }
  if (loaded.document.schema_id != kSchema ||
      loaded.document.context_id != expected_cluster_id ||
      loaded.document.state_version < minimum_state_version) {
    if (error) *error = "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH";
    return false;
  }
  SnapshotDocument decoded;
  if (!SnapshotCodec::decode(loaded.bytes, {}, &decoded, error)) return false;
  if (decoded.schema_id != kSchema || decoded.context_id != expected_cluster_id ||
      decoded.state_version < minimum_state_version) {
    if (error) *error = "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH";
    return false;
  }
  ClusterArchiveRecord candidate;
  candidate.cluster_id = decoded.context_id; candidate.state_version = decoded.state_version;
  candidate.journal_sequence = decoded.journal_sequence; candidate.payload = std::move(decoded.payload);
  *output = std::move(candidate);
  if (error) error->clear();
  return true;
}
}  // namespace surge
