#include "surge/cluster_archive_store.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "
"; std::exit(1); } } while (0)
int main() {
  using namespace surge;
  const auto path = std::filesystem::temp_directory_path() / "surge_cluster_archive_test.bin";
  std::error_code ec;
  std::filesystem::remove(path, ec); std::filesystem::remove(path.string()+".bak", ec);
  std::filesystem::remove(path.string()+".tmp", ec);
  ClusterArchiveStore store(path.u8string());
  std::string error;
  CHECK(!store.archive("", 1, 1, "payload", &error));
  CHECK(error == "CLUSTER_ARCHIVE_INPUT_INVALID");
  CHECK(!store.archive("cluster-a", 0, 1, "payload", &error));
  CHECK(store.archive("cluster-a", 7, 12, "large-ish payload", &error));
  ClusterArchiveRecord record;
  CHECK(store.restore("cluster-a", 7, &record, &error));
  CHECK(record.cluster_id == "cluster-a" && record.state_version == 7);
  CHECK(record.journal_sequence == 12 && record.payload == "large-ish payload");
  CHECK(!store.restore("cluster-b", 1, &record, &error));
  CHECK(error == "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH");
  CHECK(!store.restore("cluster-a", 8, &record, &error));
  CHECK(error == "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH");
  CHECK(store.archive("cluster-a", 8, 14, "new payload", &error));
  CHECK(store.restore("cluster-a", 8, &record, &error));
  CHECK(record.state_version == 8 && record.payload == "new payload");
  { std::ofstream out(path, std::ios::binary | std::ios::trunc);
    CHECK(static_cast<bool>(out)); out << "corrupt"; CHECK(static_cast<bool>(out)); }
  CHECK(store.restore("cluster-a", 7, &record, &error));
  CHECK(record.state_version == 7 && record.payload == "large-ish payload");
  std::filesystem::remove(path, ec); std::filesystem::remove(path.string()+".bak", ec);
  std::filesystem::remove(path.string()+".tmp", ec);
  std::cout << "SURGE cluster archive store tests passed
";
  return 0;
}
