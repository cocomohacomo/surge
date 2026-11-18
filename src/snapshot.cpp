#include "surge/snapshot.hpp"
#include "surge/sha256.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace surge {
namespace {
constexpr char kMagic[] = "SURGESNP";
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::size_t kDigestBytes = 64;
constexpr std::size_t kDefaultTotal = 64U * 1024U * 1024U;
constexpr std::size_t kDefaultPayload = 60U * 1024U * 1024U;
constexpr std::size_t kMaxMetadata = 4096U;

void put_u32(std::string* out, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8)
    out->push_back(static_cast<char>((value >> shift) & 0xffU));
}
void put_u64(std::string* out, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8)
    out->push_back(static_cast<char>((value >> shift) & 0xffU));
}
bool get_u32(const std::string& in, std::size_t* pos, std::uint32_t* value) {
  if (*pos > in.size() || in.size() - *pos < 4) return false;
  *value = 0;
  for (int i = 0; i < 4; ++i)
    *value = (*value << 8) | static_cast<unsigned char>(in[(*pos)++]);
  return true;
}
bool get_u64(const std::string& in, std::size_t* pos, std::uint64_t* value) {
  if (*pos > in.size() || in.size() - *pos < 8) return false;
  *value = 0;
  for (int i = 0; i < 8; ++i)
    *value = (*value << 8) | static_cast<unsigned char>(in[(*pos)++]);
  return true;
}
void put_string(std::string* out, const std::string& value) {
  put_u32(out, static_cast<std::uint32_t>(value.size()));
  out->append(value);
}
bool get_string(const std::string& in, std::size_t* pos,
                std::size_t maximum, std::string* value) {
  std::uint32_t size = 0;
  if (!get_u32(in, pos, &size) || size > maximum ||
      *pos > in.size() || in.size() - *pos < size) return false;
  value->assign(in.data() + *pos, size);
  *pos += size;
  return true;
}
std::size_t total_limit(const SnapshotLimits& limits) {
  return limits.max_total_bytes ? limits.max_total_bytes : kDefaultTotal;
}
std::size_t payload_limit(const SnapshotLimits& limits) {
  return limits.max_payload_bytes ? limits.max_payload_bytes : kDefaultPayload;
}
bool fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}
bool decode_impl(const std::string& bytes, const SnapshotLimits& limits,
                 SnapshotDocument* output, std::string* error,
                 bool include_payload) {
  if (!output) return fail(error, "SNAPSHOT_OUTPUT_REQUIRED");
  if (bytes.size() > total_limit(limits) || bytes.size() < 8 + 4 + 4 + 4 + 8 + 8 + 4 + kDigestBytes)
    return fail(error, "SNAPSHOT_SIZE_INVALID");
  const std::size_t content_size = bytes.size() - kDigestBytes;
  const std::string content = bytes.substr(0, content_size);
  if (sha256_hex(content) != bytes.substr(content_size))
    return fail(error, "SNAPSHOT_CHECKSUM_MISMATCH");

  std::size_t pos = 0;
  if (content.compare(0, sizeof(kMagic) - 1, kMagic) != 0)
    return fail(error, "SNAPSHOT_MAGIC_INVALID");
  pos += sizeof(kMagic) - 1;
  std::uint32_t version = 0;
  if (!get_u32(content, &pos, &version) || version != kFormatVersion)
    return fail(error, "SNAPSHOT_FORMAT_UNSUPPORTED");

  SnapshotDocument candidate;
  if (!get_string(content, &pos, kMaxMetadata, &candidate.schema_id) ||
      !get_string(content, &pos, kMaxMetadata, &candidate.context_id) ||
      candidate.schema_id.empty() || candidate.context_id.empty() ||
      !is_valid_utf8(candidate.schema_id) || !is_valid_utf8(candidate.context_id))
    return fail(error, "SNAPSHOT_METADATA_INVALID");
  if (!get_u64(content, &pos, &candidate.state_version) ||
      !get_u64(content, &pos, &candidate.journal_sequence))
    return fail(error, "SNAPSHOT_HEADER_TRUNCATED");
  std::uint32_t payload_size = 0;
  if (!get_u32(content, &pos, &payload_size) ||
      payload_size > payload_limit(limits) ||
      pos > content.size() || content.size() - pos != payload_size)
    return fail(error, "SNAPSHOT_PAYLOAD_INVALID");
  if (include_payload)
    candidate.payload.assign(content.data() + pos, payload_size);
  *output = std::move(candidate);
  if (error) error->clear();
  return true;
}
}  // namespace

bool SnapshotCodec::encode(const SnapshotDocument& doc,
                           const SnapshotLimits& limits,
                           std::string* output, std::string* error) {
  if (!output) return fail(error, "SNAPSHOT_OUTPUT_REQUIRED");
  output->clear();
  if (doc.schema_id.empty() || doc.context_id.empty())
    return fail(error, "SNAPSHOT_METADATA_REQUIRED");
  if (!is_valid_utf8(doc.schema_id) || !is_valid_utf8(doc.context_id))
    return fail(error, "SNAPSHOT_METADATA_INVALID_UTF8");
  if (doc.schema_id.size() > kMaxMetadata || doc.context_id.size() > kMaxMetadata)
    return fail(error, "SNAPSHOT_METADATA_TOO_LARGE");
  if (doc.payload.size() > payload_limit(limits) ||
      doc.payload.size() > std::numeric_limits<std::uint32_t>::max())
    return fail(error, "SNAPSHOT_PAYLOAD_LIMIT_EXCEEDED");

  std::string encoded(kMagic, sizeof(kMagic) - 1);
  put_u32(&encoded, kFormatVersion);
  put_string(&encoded, doc.schema_id);
  put_string(&encoded, doc.context_id);
  put_u64(&encoded, doc.state_version);
  put_u64(&encoded, doc.journal_sequence);
  put_u32(&encoded, static_cast<std::uint32_t>(doc.payload.size()));
  encoded.append(doc.payload);
  if (encoded.size() > total_limit(limits) ||
      kDigestBytes > total_limit(limits) - encoded.size())
    return fail(error, "SNAPSHOT_TOTAL_LIMIT_EXCEEDED");
  encoded.append(sha256_hex(encoded));
  *output = std::move(encoded);
  if (error) error->clear();
  return true;
}

bool SnapshotCodec::decode(const std::string& bytes,
                           const SnapshotLimits& limits,
                           SnapshotDocument* output, std::string* error) {
  return decode_impl(bytes, limits, output, error, true);
}

bool SnapshotCodec::inspect(const std::string& bytes,
                            const SnapshotLimits& limits,
                            SnapshotDocument* metadata, std::string* error) {
  return decode_impl(bytes, limits, metadata, error, false);
}
}  // namespace surge
