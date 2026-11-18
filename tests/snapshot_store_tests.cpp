#include "surge/snapshot_store.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)

namespace {
std::string encode(const std::string& context, std::uint64_t version,
                   const std::string& payload) {
  surge::SnapshotDocument document;
  document.schema_id = "surge.test.snapshot.v1";
  document.context_id = context;
  document.state_version = version;
  document.journal_sequence = version;
  document.payload = payload;
  std::string bytes, error;
  CHECK(surge::SnapshotCodec::encode(document, {}, &bytes, &error));
  return bytes;
}
void write_text(const std::filesystem::path& path, const std::string& value) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  CHECK(static_cast<bool>(out));
  out.write(value.data(), static_cast<std::streamsize>(value.size()));
  CHECK(static_cast<bool>(out));
}
void remove_generation(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path.string() + ".bak", ec);
  std::filesystem::remove(path.string() + ".tmp", ec);
  std::filesystem::remove(path.string() + ".bak.tmp", ec);
}
}

int main() {
  using namespace surge;
  const auto root = std::filesystem::temp_directory_path();
  const auto path = root / "surge_snapshot_store_test.bin";
  const auto limited_path = root / "surge_snapshot_store_limited.bin";
  const auto no_backup_path = root / "surge_snapshot_store_no_backup.bin";
  remove_generation(path);
  remove_generation(limited_path);
  remove_generation(no_backup_path);

  const auto generation_one = encode("ctx", 1, "state-one");
  const auto generation_two = encode("ctx", 2, "state-two");
  const auto generation_three = encode("ctx", 3, "state-three");
  std::string error;

  SnapshotStore store(path.u8string());
  CHECK(store.save(generation_one, &error));
  auto loaded = store.load();
  CHECK(loaded.ok && !loaded.used_backup);
  CHECK(loaded.bytes == generation_one);
  CHECK(loaded.document.state_version == 1);

  // A stale temporary file is never treated as a committed generation.
  write_text(path.string() + ".tmp", "incomplete-generation");
  loaded = store.load();
  CHECK(loaded.ok && !loaded.used_backup && loaded.bytes == generation_one);

  CHECK(store.save(generation_two, &error));
  loaded = store.load();
  CHECK(loaded.ok && !loaded.used_backup);
  CHECK(loaded.bytes == generation_two && loaded.document.state_version == 2);

  // Invalid input is rejected before touching either committed generation.
  CHECK(!store.save("not-a-snapshot", &error));
  loaded = store.load();
  CHECK(loaded.ok && loaded.bytes == generation_two);

  // Simulate a torn/corrupted current generation. The durable previous
  // generation must remain independently readable as the recovery source.
  write_text(path, "corrupt-current-generation");
  loaded = store.load();
  CHECK(loaded.ok && loaded.used_backup);
  CHECK(loaded.bytes == generation_one);
  CHECK(loaded.document.context_id == "ctx");
  CHECK(loaded.document.state_version == 1);

  SnapshotStoreLimits small_limits;
  small_limits.max_file_bytes = generation_one.size() - 1;
  SnapshotStore limited(limited_path.u8string(), small_limits);
  CHECK(!limited.save(generation_one, &error));
  CHECK(!limited.load().ok);

  SnapshotStoreLimits no_backup_limits;
  no_backup_limits.keep_backup = false;
  SnapshotStore no_backup(no_backup_path.u8string(), no_backup_limits);
  CHECK(no_backup.save(generation_one, &error));
  CHECK(no_backup.save(generation_two, &error));
  write_text(no_backup_path, "corrupt-current-generation");
  CHECK(!no_backup.load().ok);

  // Backup recovery is read-only: it does not silently rewrite the primary.
  CHECK(std::filesystem::file_size(path) ==
        std::string("corrupt-current-generation").size());

  remove_generation(path);
  remove_generation(limited_path);
  remove_generation(no_backup_path);
  std::cout << "SURGE durable snapshot store tests passed\n";
  return 0;
}
