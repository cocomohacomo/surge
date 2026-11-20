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
void patch_u32(std::string* out, std::size_t pos, std::uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8)
    (*out)[pos++] = static_cast<char>((v >> shift) & 0xffU);
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
bool reserve_restore_staging(ResourceBudget* budget, std::size_t amount,
    std::size_t maximum, ResourceLease& lease, std::string* error) {
  if (amount > maximum)
    return fail(error, "SNAPSHOT_RESTORE_STAGING_LIMIT_EXCEEDED");
  if (!budget || amount == 0) return true;
  std::string budget_error;
  if (!budget->try_acquire(ResourceKind::TransientBytes, amount, lease,
                           budget_error))
    return fail(error, budget_error.empty()
        ? "SNAPSHOT_RESTORE_RESOURCE_BUDGET_EXCEEDED"
        : budget_error.c_str());
  return true;
}
bool checked_add(std::size_t a, std::size_t b, std::size_t* out) {
  if (b > std::numeric_limits<std::size_t>::max() - a) return false;
  *out = a + b;
  return true;
}
struct PreparedEntry {
  SnapshotDomain* domain;
  std::unique_ptr<PreparedSnapshotRestore> restore;
};
}  // namespace

bool SnapshotDomain::capture_chunks(const SnapshotChunkWriter& writer,
                                     std::string* error) const {
  if (!writer) return fail(error, "SNAPSHOT_CHUNK_WRITER_REQUIRED");
  std::string payload;
  if (!capture(&payload, error)) return false;
  constexpr std::size_t kChunkBytes = 16U * 1024U;
  for (std::size_t offset = 0; offset < payload.size();) {
    const std::size_t amount = std::min(kChunkBytes, payload.size() - offset);
    if (!writer(payload.data() + offset, amount, error)) return false;
    offset += amount;
  }
  if (error) error->clear();
  return true;
}

SnapshotCoordinator::SnapshotCoordinator(SnapshotCoordinatorLimits limits,
    ResourceBudget* budget)
    : limits_(limits), budget_(budget) {}

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
  ResourceLease capture_staging;
  std::size_t accounted_capture_bytes = 0;
  const std::size_t capture_limit = limits_.max_capture_staging_bytes == 0
      ? 64U * 1024U * 1024U : limits_.max_capture_staging_bytes;
  const std::size_t aggregate_limit = limits_.envelope.max_payload_bytes == 0
      ? 60U * 1024U * 1024U : limits_.envelope.max_payload_bytes;
  const auto account_capture_bytes = [&](std::size_t amount,
                                         std::string* account_error) {
    std::size_t next = 0;
    if (!checked_add(accounted_capture_bytes, amount, &next) ||
        next > capture_limit)
      return fail(account_error, "SNAPSHOT_CAPTURE_STAGING_LIMIT_EXCEEDED");
    if (budget_ && amount != 0) {
      std::string budget_error;
      const bool ok = capture_staging
          ? capture_staging.grow(amount, budget_error)
          : budget_->try_acquire(ResourceKind::TransientBytes, amount,
                                 capture_staging, budget_error);
      if (!ok)
        return fail(account_error, budget_error.empty()
            ? "SNAPSHOT_CAPTURE_RESOURCE_BUDGET_EXCEEDED"
            : budget_error.c_str());
    }
    accounted_capture_bytes = next;
    if (account_error) account_error->clear();
    return true;
  };
  if (!account_capture_bytes(4, error)) return false;
  put_u32(&bundle, static_cast<std::uint32_t>(domains_.size()));
  for (const auto* domain : domains_) {
    const std::string id = domain->domain_id();
    const std::string schema = domain->schema_id();
    if (!is_valid_utf8(id) || !is_valid_utf8(schema) ||
        id.size() > 4096 || schema.size() > 4096)
      return fail(error, "SNAPSHOT_BUNDLE_FIELD_INVALID");
    std::size_t frame_bytes = 12;
    if (!checked_add(frame_bytes, id.size(), &frame_bytes) ||
        !checked_add(frame_bytes, schema.size(), &frame_bytes) ||
        !account_capture_bytes(frame_bytes, error)) return false;
    if (!put_field(&bundle, id, 4096) || !put_field(&bundle, schema, 4096))
      return fail(error, "SNAPSHOT_BUNDLE_FIELD_INVALID");
    const std::size_t length_offset = bundle.size();
    put_u32(&bundle, 0);
    std::size_t payload_size = 0;
    std::string domain_error;
    const SnapshotChunkWriter writer = [&](const char* data, std::size_t size,
                                            std::string* chunk_error) {
      if (size != 0 && data == nullptr)
        return fail(chunk_error, "SNAPSHOT_DOMAIN_CHUNK_INVALID");
      const std::size_t chunk_limit = limits_.max_capture_chunk_bytes == 0
          ? 64U * 1024U : limits_.max_capture_chunk_bytes;
      if (size > chunk_limit)
        return fail(chunk_error, "SNAPSHOT_CAPTURE_CHUNK_LIMIT_EXCEEDED");
      if (size > limits_.max_domain_payload_bytes -
                     std::min(payload_size, limits_.max_domain_payload_bytes))
        return fail(chunk_error, "SNAPSHOT_DOMAIN_PAYLOAD_LIMIT_EXCEEDED");
      if (size > std::numeric_limits<std::uint32_t>::max() - payload_size)
        return fail(chunk_error, "SNAPSHOT_DOMAIN_PAYLOAD_LIMIT_EXCEEDED");
      std::size_t next_bundle = 0;
      if (!checked_add(bundle.size(), size, &next_bundle) ||
          next_bundle > aggregate_limit || next_bundle > capture_limit)
        return fail(chunk_error, "SNAPSHOT_CAPTURE_STAGING_LIMIT_EXCEEDED");
      if (!account_capture_bytes(size, chunk_error)) return false;
      if (size != 0) bundle.append(data, size);
      payload_size += size;
      if (chunk_error) chunk_error->clear();
      return true;
    };
    if (!domain->capture_chunks(writer, &domain_error))
      return fail(error, domain_error.empty()
          ? "SNAPSHOT_DOMAIN_CAPTURE_FAILED" : domain_error.c_str());
    patch_u32(&bundle, length_offset, static_cast<std::uint32_t>(payload_size));
  }
  SnapshotDocument document;
  document.schema_id = "surge.snapshot.bundle.v1";
  document.context_id = context_id;
  document.state_version = state_version;
  document.journal_sequence = journal_sequence;
  std::size_t encode_peak = bundle.size();
  if (!checked_add(encode_peak, bundle.size(), &encode_peak) ||
      !checked_add(encode_peak, context_id.size(), &encode_peak) ||
      !checked_add(encode_peak, 128, &encode_peak) ||
      encode_peak > capture_limit)
    return fail(error, "SNAPSHOT_CAPTURE_STAGING_LIMIT_EXCEEDED");
  if (encode_peak < accounted_capture_bytes ||
      !account_capture_bytes(encode_peak - accounted_capture_bytes, error))
    return false;
  document.payload = std::move(bundle);
  return SnapshotCodec::encode(document, limits_.envelope, bytes, error);
}

bool SnapshotCoordinator::restore(const std::string& bytes,
    const std::string& expected_context_id, std::uint64_t minimum_state_version,
    std::uint64_t minimum_journal_sequence, std::string* error) {
  ResourceLease input_staging;
  if (!reserve_restore_staging(budget_, bytes.size(),
      limits_.max_restore_staging_bytes, input_staging, error)) return false;
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
  ResourceLease domain_staging;
  if (!reserve_restore_staging(budget_, bundle.size(),
      limits_.max_restore_staging_bytes >= bytes.size()
          ? limits_.max_restore_staging_bytes - bytes.size() : 0,
      domain_staging, error)) return false;
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
  std::size_t upfront_staging = 0;
  if (!checked_add(bytes.size(), tail_limits.max_total_bytes, &upfront_staging) ||
      upfront_staging > limits_.max_restore_staging_bytes)
    return fail(error, "SNAPSHOT_RESTORE_STAGING_LIMIT_EXCEEDED");
  ResourceLease input_staging, tail_capacity_staging;
  if (!reserve_restore_staging(budget_, bytes.size(),
      limits_.max_restore_staging_bytes, input_staging, error)) return false;
  if (!reserve_restore_staging(budget_, tail_limits.max_total_bytes,
      limits_.max_restore_staging_bytes - bytes.size(),
      tail_capacity_staging, error)) return false;
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
  const auto checkpoint = journal.checkpoint_info();
  if (checkpoint.compacted) {
    if (document.context_id != checkpoint.context_id)
      return fail(error, "SNAPSHOT_JOURNAL_CHECKPOINT_CONTEXT_MISMATCH");
    if (document.state_version < checkpoint.base_state_version)
      return fail(error, "SNAPSHOT_CHECKPOINT_BEHIND_COMPACTION_BASE");
    if (document.state_version == checkpoint.base_state_version &&
        sha256_hex(bytes) != checkpoint.snapshot_digest)
      return fail(error, "SNAPSHOT_JOURNAL_CHECKPOINT_DIGEST_MISMATCH");
  }
  if (domains_.empty())
    return fail(error, "SNAPSHOT_NO_DOMAINS_REGISTERED");

  const auto tail = journal.committed_tail(document.journal_sequence, tail_limits);
  if (!tail.ok)
    return fail(error, tail.error.empty() ? "SNAPSHOT_JOURNAL_TAIL_UNAVAILABLE" : tail.error.c_str());
  if (tail.total_bytes > tail_limits.max_total_bytes)
    return fail(error, "SNAPSHOT_JOURNAL_TAIL_LIMIT_EXCEEDED");
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

bool SnapshotCoordinator::restore_from_store(
    SnapshotStore& store, EventJournal& journal,
    const std::string& expected_context_id,
    std::uint64_t minimum_state_version,
    std::uint64_t minimum_journal_sequence,
    JournalTailLimits tail_limits, std::string* error) {
  if (domains_.empty())
    return fail(error, "SNAPSHOT_NO_DOMAINS_REGISTERED");

  SnapshotDocument metadata;
  std::vector<PreparedEntry> prepared;
  std::vector<JournalEvent> tail;
  ResourceLease tail_staging;
  ResourceLease payload_staging;
  std::size_t tail_state_version = 0;
  std::string pending;
  std::size_t consumed = 0;
  std::size_t domain_index = 0;
  std::uint32_t domain_count = 0;
  std::uint32_t field_size = 0;
  std::uint32_t payload_size = 0;
  std::string domain_id, schema_id;
  int parser_state = 0;  // count, id length/data, schema length/data, payload length/data, complete
  bool metadata_checked = false;
  bool tail_ready = false;
  bool parser_failed = false;
  std::string parser_error;

  const auto read_u32 = [&](std::uint32_t* value) {
    if (pending.size() - consumed < 4) return false;
    *value = 0;
    for (int i = 0; i < 4; ++i)
      *value = (*value << 8) |
          static_cast<unsigned char>(pending[consumed++]);
    return true;
  };
  const auto sink = [&](const char* data, std::size_t size,
                        std::string* sink_error) {
    if (!metadata_checked) {
      metadata_checked = true;
      if (metadata.schema_id != "surge.snapshot.bundle.v1")
        return fail(sink_error, "SNAPSHOT_BUNDLE_SCHEMA_MISMATCH");
      if (metadata.context_id != expected_context_id)
        return fail(sink_error, "SNAPSHOT_CONTEXT_MISMATCH");
      if (metadata.state_version < minimum_state_version ||
          metadata.journal_sequence < minimum_journal_sequence)
        return fail(sink_error, "SNAPSHOT_WATERMARK_REGRESSION");
      if (metadata.state_version != metadata.journal_sequence)
        return fail(sink_error, "SNAPSHOT_JOURNAL_WATERMARK_MISMATCH");
      const auto checkpoint = journal.checkpoint_info();
      if (checkpoint.compacted) {
        if (metadata.context_id != checkpoint.context_id)
          return fail(sink_error, "SNAPSHOT_JOURNAL_CHECKPOINT_CONTEXT_MISMATCH");
        if (metadata.state_version < checkpoint.base_state_version)
          return fail(sink_error, "SNAPSHOT_CHECKPOINT_BEHIND_COMPACTION_BASE");
        // The streamed API deliberately does not materialize the envelope,
        // so an equal-watermark byte-for-byte checkpoint comparison is not
        // available. Fail closed rather than skipping the digest check.
        if (metadata.state_version == checkpoint.base_state_version)
          return fail(sink_error, "SNAPSHOT_JOURNAL_CHECKPOINT_STREAM_DIGEST_UNAVAILABLE");
      }
      const auto staged_tail = journal.committed_tail(
          metadata.journal_sequence, tail_limits);
      if (!staged_tail.ok)
        return fail(sink_error, staged_tail.error.empty()
            ? "SNAPSHOT_JOURNAL_TAIL_UNAVAILABLE" : staged_tail.error.c_str());
      if (staged_tail.total_bytes > tail_limits.max_total_bytes)
        return fail(sink_error, "SNAPSHOT_JOURNAL_TAIL_LIMIT_EXCEEDED");
      if (staged_tail.current_state_version < metadata.state_version)
        return fail(sink_error, "SNAPSHOT_JOURNAL_BEHIND_CHECKPOINT");
      if (staged_tail.current_state_version != journal.state_version())
        return fail(sink_error, "SNAPSHOT_JOURNAL_ADVANCED_DURING_RESTORE");
      tail = staged_tail.events;
      tail_state_version = staged_tail.current_state_version;
      if (budget_ && tail_limits.max_total_bytes != 0) {
        std::string budget_error;
        if (!budget_->try_acquire(ResourceKind::TransientBytes,
                tail_limits.max_total_bytes, tail_staging, budget_error))
          return fail(sink_error, budget_error.empty()
              ? "SNAPSHOT_RESTORE_RESOURCE_BUDGET_EXCEEDED"
              : budget_error.c_str());
      }
      if (tail_limits.max_total_bytes > limits_.max_restore_staging_bytes)
        return fail(sink_error, "SNAPSHOT_RESTORE_STAGING_LIMIT_EXCEEDED");
      tail_ready = true;
    }
    if (parser_failed) return fail(sink_error, parser_error.c_str());
    if (size != 0 && data == nullptr)
      return fail(sink_error, "SNAPSHOT_DOMAIN_CHUNK_INVALID");
    if (size > 64U * 1024U)
      return fail(sink_error, "SNAPSHOT_RESTORE_CHUNK_LIMIT_EXCEEDED");
    if (size > limits_.max_restore_staging_bytes -
            std::min(pending.size(), limits_.max_restore_staging_bytes))
      return fail(sink_error, "SNAPSHOT_RESTORE_STAGING_LIMIT_EXCEEDED");
    if (size != 0) pending.append(data, size);

    while (true) {
      if (parser_state == 0) {
        if (!read_u32(&domain_count)) break;
        if (domain_count != domains_.size() ||
            domain_count > limits_.max_domains)
          return fail(sink_error, "SNAPSHOT_DOMAIN_SET_MISMATCH");
        prepared.reserve(domain_count);
        parser_state = domain_count == 0 ? 7 : 1;
      } else if (parser_state == 1) {
        if (!read_u32(&field_size)) break;
        if (field_size > 4096)
          return fail(sink_error, "SNAPSHOT_BUNDLE_FIELD_INVALID");
        parser_state = 2;
      } else if (parser_state == 2) {
        if (pending.size() - consumed < field_size) break;
        domain_id.assign(pending.data() + consumed, field_size);
        consumed += field_size;
        parser_state = 3;
      } else if (parser_state == 3) {
        if (!read_u32(&field_size)) break;
        if (field_size > 4096)
          return fail(sink_error, "SNAPSHOT_BUNDLE_FIELD_INVALID");
        parser_state = 4;
      } else if (parser_state == 4) {
        if (pending.size() - consumed < field_size) break;
        schema_id.assign(pending.data() + consumed, field_size);
        consumed += field_size;
        parser_state = 5;
      } else if (parser_state == 5) {
        if (!read_u32(&payload_size)) break;
        if (payload_size > limits_.max_domain_payload_bytes)
          return fail(sink_error, "SNAPSHOT_DOMAIN_PAYLOAD_LIMIT_EXCEEDED");
        std::size_t payload_reservation = 0;
        if (!checked_add(static_cast<std::size_t>(payload_size),
                         static_cast<std::size_t>(payload_size),
                         &payload_reservation) ||
            payload_reservation > limits_.max_restore_staging_bytes)
          return fail(sink_error, "SNAPSHOT_RESTORE_STAGING_LIMIT_EXCEEDED");
        if (budget_ && payload_reservation != 0) {
          std::string budget_error;
          if (!budget_->try_acquire(ResourceKind::TransientBytes,
                  payload_reservation, payload_staging, budget_error))
            return fail(sink_error, budget_error.empty()
                ? "SNAPSHOT_RESTORE_RESOURCE_BUDGET_EXCEEDED"
                : budget_error.c_str());
        }
        parser_state = 6;
      } else if (parser_state == 6) {
        if (pending.size() - consumed < payload_size) break;
        if (domain_index >= domains_.size())
          return fail(sink_error, "SNAPSHOT_DOMAIN_SET_MISMATCH");
        SnapshotDomain* domain = domains_[domain_index];
        if (!is_valid_utf8(domain_id) || !is_valid_utf8(schema_id) ||
            domain_id != domain->domain_id() ||
            schema_id != domain->schema_id())
          return fail(sink_error, "SNAPSHOT_DOMAIN_SCHEMA_MISMATCH");
        std::string payload(pending.data() + consumed, payload_size);
        consumed += payload_size;
        std::string domain_error;
        auto staged = domain->prepare_restore_with_tail(
            payload, tail, &domain_error);
        if (!staged)
          return fail(sink_error, domain_error.empty()
              ? "SNAPSHOT_DOMAIN_TAIL_PREPARE_FAILED"
              : domain_error.c_str());
        prepared.push_back({domain, std::move(staged)});
        payload_staging.reset();
        ++domain_index;
        parser_state = domain_index == domain_count ? 7 : 1;
      } else {
        break;
      }
    }
    if (consumed != 0) {
      pending.erase(0, consumed);
      consumed = 0;
    }
    if (parser_state == 7 && !pending.empty())
      return fail(sink_error, "SNAPSHOT_BUNDLE_TRAILING_DATA");
    if (sink_error) sink_error->clear();
    return true;
  };

  if (!store.load_stream(&metadata, sink, nullptr, error))
    return false;
  if (!metadata_checked || !tail_ready)
    return fail(error, "SNAPSHOT_STREAM_EMPTY_PAYLOAD");
  if (parser_state != 7 || domain_index != domains_.size() ||
      !pending.empty())
    return fail(error, "SNAPSHOT_BUNDLE_TRUNCATED");
  if (journal.state_version() != tail_state_version)
    return fail(error, "SNAPSHOT_JOURNAL_ADVANCED_DURING_RESTORE");

  // Every domain has prepared successfully. Publish only after the complete
  // authenticated bundle and journal-tail replay have passed validation.
  for (auto& entry : prepared) entry.restore->commit();
  if (error) error->clear();
  return true;
}

bool SnapshotCoordinator::compact(EventJournal& journal, SnapshotStore& store,
                                  const std::string& context_id,
                                  std::string* error) {
  const auto version = journal.state_version();
  std::string bytes;
  if (!capture(context_id, version, version, &bytes, error))
    return false;
  if (journal.state_version() != version)
    return fail(error, "SNAPSHOT_JOURNAL_ADVANCED_DURING_CAPTURE");
  if (!store.save(bytes, error))
    return false;
  const auto verified = store.load();
  if (!verified.ok || verified.used_backup || verified.bytes != bytes)
    return fail(error, "SNAPSHOT_STORE_DURABLE_VERIFY_FAILED");
  // Mirror the exact checkpoint before journal replacement. Thus either
  // journal generation can be paired with a valid copy at the same watermark.
  if (!store.mirror_current_to_backup(error))
    return false;
  if (journal.state_version() != version)
    return fail(error, "SNAPSHOT_JOURNAL_ADVANCED_DURING_COMPACTION");
  if (!journal.compact_after_checkpoint(bytes, context_id, error))
    return false;
  return true;
}

}  // namespace surge
