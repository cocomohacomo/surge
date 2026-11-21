#include "surge/snapshot.hpp"
#include "surge/sha256.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)

int main() {
  using namespace surge;
  SnapshotDocument source;
  source.schema_id = "surge.context-state.v1";
  source.context_id = "ctx-test";
  source.state_version = 42;
  source.journal_sequence = 109;
  source.payload = std::string("opaque\0state", 12);

  std::string encoded, error;
  CHECK(SnapshotCodec::encode(source, {}, &encoded, &error));
  CHECK(error.empty());
  const std::string valid_encoded = encoded;
  SnapshotDocument restored;
  CHECK(SnapshotCodec::decode(encoded, {}, &restored, &error));
  CHECK(restored.schema_id == source.schema_id);
  CHECK(restored.context_id == source.context_id);
  CHECK(restored.state_version == source.state_version);
  CHECK(restored.journal_sequence == source.journal_sequence);
  CHECK(restored.payload == source.payload);
  SnapshotDocument metadata;
  CHECK(SnapshotCodec::inspect(valid_encoded, {}, &metadata, &error));
  CHECK(metadata.context_id == source.context_id);
  CHECK(metadata.state_version == source.state_version);
  CHECK(metadata.payload.empty());

  std::string damaged = encoded;
  damaged[damaged.size() / 2] ^= 0x01;
  CHECK(!SnapshotCodec::decode(damaged, {}, &restored, &error));
  CHECK(error == "SNAPSHOT_CHECKSUM_MISMATCH");

  SnapshotLimits tiny;
  tiny.max_payload_bytes = 4;
  CHECK(!SnapshotCodec::encode(source, tiny, &encoded, &error));
  CHECK(error == "SNAPSHOT_PAYLOAD_LIMIT_EXCEEDED");

  SnapshotDocument unchanged;
  unchanged.context_id = "sentinel";
  CHECK(!SnapshotCodec::decode(valid_encoded + "trailing", {}, &unchanged, &error));
  CHECK(unchanged.context_id == "sentinel");
  std::cout << "SURGE snapshot codec tests passed\n";
  return 0;
}
