#include "surge/snapshot_coordinator.hpp"
#include "surge/sha256.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace surge {
namespace {
void put_u32(std::string* out, std::uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8)
    out->push_back(static_cast<char>((v >> shift) & 0xffU));
}
bool get_u32(const std::string& in, std::size_t* pos, std::uint32_t* v) {
  if (*pos > in.size() || in.size() - *pos < 4) return false;
  *v = 0;
  for (int i = 0; i < 4; ++i)
    *v = (*v << 8) | static_cast<unsigned char>(in[(*pos)++]);
  return true;
}
bool put_field(std::string* out, const std::string& value,
               std::size_t maximum) {
  if (value.size() > maximum ||
      value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
  put_u32(out, static_cast<std::uint32_t>(value.size()));
  out->append(value);
  return true;
}
bool get_field(const std::string& in, std::size_t* pos,
               std::size_t maximum, std::string* value) {
  std::uint32_t size = 0;
  if (!get_u32(in, pos, &size) || size > maximum ||
      *pos > in.size() || in.size() - *pos < size) return false;
  value->assign(in.data() + *pos, size);
  *pos += size;
  return true;
}
bool fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}
struct PreparedEntry {
  SnapshotDomain* domain;
  std::unique_ptr<PreparedSnapshotRestore> restore;
};
}  // namespace

SnapshotCoordinator::SnapshotCoordinator(SnapshotCoordinatorLimits limits)
    : limits_(limits) {}

bool SnapshotCoordinator::register_domain(SnapshotDomain* domain,
                                          std::string* error) {
  if (!domain || domain->domain_id().empty() || domain->schema_id().empty() ||
      !is_valid_utf8(domain->domain_id()) || !is_valid_utf8(domain->schema_id()))
    return fail(error, "SNAPSHOT_DOMAIN_INVALID");
  if (domains_.size() >= limits_.max_domains)
    return fail(error, "SNAPSHOT_DOMAIN_LIMIT_EXCEEDED");
  for (const auto* existing : domains_)
    if (existing->domain_id() == domain->domain_id())
      return fail(error, "SNAPSHOT_DOMAIN_DUPLICATE");
  domains_.push_back(domain);
  std::sort(domains_.begin(), domains_.end(),
      [](const SnapshotDomain* a, const SnapshotDomain* b) {
        return a->domain_id() < b->domain_id();
      });
  if (error) error->clear();
  return true;
}

bool SnapshotCoordinator::capture(const std::string& context_id,
    std::uint64_t state_version, std::uint64_t journal_sequence,
    std::string* bytes, std::string* error) const {
  if (!bytes) return fail(error, "SNAPSHOT_OUTPUT_REQUIRED");
  bytes->clear();
  if (domains_.empty()) return fail(error, "SNAPSHOT_NO_DOMAINS_REGISTERED");
  if (domains_.size() > std::numeric_limits<std::uint32_t>::max())
    return fail(error, "SNAPSHOT_DOMAIN_COUNT_INVALID");
  std::string bundle;
  put_u32(&bundle, static_cast<std::uint32_t>(domains_.size()));
  for (const auto* domain : domains_) {
    std::string payload, domain_error;
    if (!domain->capture(&payload, &domain_error))
      return fail(error, domain_error.empty() ? "SNAPSHOT_DOMAIN_CAPTURE_FAILED" : domain_error.c_str());
    if (payload.size() > limits_.max_domain_payload_bytes)
      return fail(error, "SNAPSHOT_DOMAIN_PAYLOAD_LIMIT_EXCEEDED");
    if (!is_valid_utf8(domain->domain_id()) || !is_valid_utf8(domain->schema_id()) ||
        !put_field(&bundle, domain->domain_id(), 4096) ||
        !put_field(&bundle, domain->schema_id(), 4096) ||
        !put_field(&bundle, payload, limits_.max_domain_payload_bytes))
      return fail(error, "SNAPSHOT_BUNDLE_FIELD_INVALID");
    const std::size_t aggregate_limit = limits_.envelope.max_payload_bytes == 0
        ? 60U * 1024U * 1024U : limits_.envelope.max_payload_bytes;
    if (bundle.size() > aggregate_limit)
      return fail(error, "SNAPSHOT_BUNDLE_LIMIT_EXCEEDED");
  }
  SnapshotDocument document;
  document.schema_id = "surge.snapshot.bundle.v1";
  document.context_id = context_id;
  document.state_version = state_version;
  document.journal_sequence = journal_sequence;
  document.payload = std::move(bundle);
  return SnapshotCodec::encode(document, limits_.envelope, bytes, error);
}

bool SnapshotCoordinator::restore(const std::string& bytes,
    const std::string& expected_context_id, std::uint64_t minimum_state_version,
    std::uint64_t minimum_journal_sequence, std::string* error) {
  SnapshotDocument document;
  if (!SnapshotCodec::decode(bytes, limits_.envelope, &document, error))
    return false;
  if (document.schema_id != "surge.snapshot.bundle.v1")
    return fail(error, "SNAPSHOT_BUNDLE_SCHEMA_MISMATCH");
  if (document.context_id != expected_context_id)
    return fail(error, "SNAPSHOT_CONTEXT_MISMATCH");
  if (document.state_version < minimum_state_version ||
      document.journal_sequence < minimum_journal_sequence)
    return fail(error, "SNAPSHOT_WATERMARK_REGRESSION");

  const std::string& bundle = document.payload;
  std::size_t pos = 0;
  std::uint32_t count = 0;
  if (!get_u32(bundle, &pos, &count) || count != domains_.size() ||
      count > limits_.max_domains)
    return fail(error, "SNAPSHOT_DOMAIN_SET_MISMATCH");

  std::vector<PreparedEntry> prepared;
  prepared.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::string id, schema, payload;
    if (!get_field(bundle, &pos, 4096, &id) ||
        !get_field(bundle, &pos, 4096, &schema) ||
        !get_field(bundle, &pos, limits_.max_domain_payload_bytes, &payload))
      return fail(error, "SNAPSHOT_BUNDLE_TRUNCATED");
    SnapshotDomain* domain = domains_[i];
    if (!is_valid_utf8(id) || !is_valid_utf8(schema) ||
        id != domain->domain_id() || schema != domain->schema_id())
      return fail(error, "SNAPSHOT_DOMAIN_SCHEMA_MISMATCH");
    std::string domain_error;
    auto staged = domain->prepare_restore(payload, &domain_error);
    if (!staged)
      return fail(error, domain_error.empty() ? "SNAPSHOT_DOMAIN_PREPARE_FAILED" : domain_error.c_str());
    prepared.push_back({domain, std::move(staged)});
  }
  if (pos != bundle.size()) return fail(error, "SNAPSHOT_BUNDLE_TRAILING_DATA");

  // No live domain is mutated until every domain has prepared successfully.
  for (auto& entry : prepared) entry.restore->commit();
  if (error) error->clear();
  return true;
}

bool SnapshotCoordinator::restore_with_tail(const std::string& bytes,
    EventJournal& journal, const std::string& expected_context_id,
    std::uint64_t minimum_state_version,
    std::uint64_t minimum_journal_sequence,
    JournalTailLimits tail_limits, std::string* error) {
  SnapshotDocument document;
  if (!SnapshotCodec::decode(bytes, limits_.envelope, &document, error))
    return false;
  if (document.schema_id != "surge.snapshot.bundle.v1")
    return fail(error, "SNAPSHOT_BUNDLE_SCHEMA_MISMATCH");
  if (document.context_id != expected_context_id)
    return fail(error, "SNAPSHOT_CONTEXT_MISMATCH");
  if (document.state_version < minimum_state_version ||
      document.journal_sequence < minimum_journal_sequence)
    return fail(error, "SNAPSHOT_WATERMARK_REGRESSION");
  // EventJournal currently uses one monotonically increasing transaction
  // watermark for both fields. Refuse ambiguous checkpoint coordinates.
  if (document.state_version != document.journal_sequence)
    return fail(error, "SNAPSHOT_JOURNAL_WATERMARK_MISMATCH");
  if (domains_.empty())
    return fail(error, "SNAPSHOT_NO_DOMAINS_REGISTERED");

  const auto tail = journal.committed_tail(document.journal_sequence, tail_limits);
  if (!tail.ok)
    return fail(error, tail.error.empty() ? "SNAPSHOT_JOURNAL_TAIL_UNAVAILABLE" : tail.error.c_str());
  if (tail.current_state_version < document.state_version)
    return fail(error, "SNAPSHOT_JOURNAL_BEHIND_CHECKPOINT");

  const std::string& bundle = document.payload;
  std::size_t pos = 0;
  std::uint32_t count = 0;
  if (!get_u32(bundle, &pos, &count) || count != domains_.size() ||
      count > limits_.max_domains)
    return fail(error, "SNAPSHOT_DOMAIN_SET_MISMATCH");

  std::vector<PreparedEntry> prepared;
  prepared.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::string id, schema, payload;
    if (!get_field(bundle, &pos, 4096, &id) ||
        !get_field(bundle, &pos, 4096, &schema) ||
        !get_field(bundle, &pos, limits_.max_domain_payload_bytes, &payload))
      return fail(error, "SNAPSHOT_BUNDLE_TRUNCATED");
    SnapshotDomain* domain = domains_[i];
    if (!is_valid_utf8(id) || !is_valid_utf8(schema) ||
        id != domain->domain_id() || schema != domain->schema_id())
      return fail(error, "SNAPSHOT_DOMAIN_SCHEMA_MISMATCH");
    std::string domain_error;
    auto staged = domain->prepare_restore_with_tail(payload, tail.events, &domain_error);
    if (!staged)
      return fail(error, domain_error.empty() ? "SNAPSHOT_DOMAIN_TAIL_PREPARE_FAILED" : domain_error.c_str());
    prepared.push_back({domain, std::move(staged)});
  }
  if (pos != bundle.size())
    return fail(error, "SNAPSHOT_BUNDLE_TRAILING_DATA");
  // The application must quiesce writers. This final watermark check catches
  // accidental concurrent commits during staging before any state is published.
  if (journal.state_version() != tail.current_state_version)
    return fail(error, "SNAPSHOT_JOURNAL_ADVANCED_DURING_RESTORE");

  for (auto& entry : prepared) entry.restore->commit();
  if (error) error->clear();
  return true;
}
}  // namespace surge
