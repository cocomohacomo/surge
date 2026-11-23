#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace surge {

// A domain-neutral checkpoint. payload is owned by the domain serializer;
// SURGE does not infer or rewrite semantic state while encoding it.
struct SnapshotDocument {
  std::string schema_id;
  std::string context_id;
  std::uint64_t state_version{0};
  std::uint64_t journal_sequence{0};
  std::string payload;
  // SHA-256 of the complete authenticated on-disk envelope, populated by
  // SnapshotStore::load_stream without materializing the envelope.
  std::string envelope_digest;
};

struct SnapshotLimits {
  // Zero selects the conservative defaults: 64 MiB total, 60 MiB payload.
  std::size_t max_total_bytes{0};
  std::size_t max_payload_bytes{0};
};

struct SnapshotResult {
  bool ok{false};
  SnapshotDocument document;
  std::string bytes;
  std::string error;
};

// Versioned, length-delimited binary envelope with SHA-256 integrity check.
// This codec does not persist files or authorize journal truncation.
class SnapshotCodec {
 public:
  static bool encode(const SnapshotDocument& document,
                    const SnapshotLimits& limits,
                    std::string* output, std::string* error);
  static bool decode(const std::string& bytes,
                    const SnapshotLimits& limits,
                    SnapshotDocument* output, std::string* error);
  // Validates the complete envelope and returns metadata without copying the
  // potentially large opaque payload into the output document.
  static bool inspect(const std::string& bytes,
                      const SnapshotLimits& limits,
                      SnapshotDocument* metadata, std::string* error);
};

}  // namespace surge
