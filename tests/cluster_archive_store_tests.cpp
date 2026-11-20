#include "surge/cluster_archive_store.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)
int main() {
  using namespace surge;
  const auto path = std::filesystem::temp_directory_path() / "surge_cluster_archive_test.bin";
  std::error_code ec;
  const std::string graph_path = "cluster_archive_detach_plan_test.journal";
  std::remove(graph_path.c_str());
  {
    EventJournal journal(graph_path);
    CHECK(journal.open());
    ContextGraph graph(journal);
    CHECK(graph.open());
    SemanticDelta seed;
    seed.delta_id = "detach-plan-seed";
    seed.idempotency_key = "detach-plan-seed-key";
    seed.expected_state_version = graph.state_version();
    seed.nodes.push_back({{"archive-a", NodeKind::Cluster, "archive target", {}}});
    seed.nodes.push_back({{"archive-b", NodeKind::Cluster, "other owner", {}}});
    seed.nodes.push_back({{"exclusive", NodeKind::Entity, "exclusive payload", {}}});
    seed.nodes.push_back({{"shared", NodeKind::Entity, "shared payload", {}}});
    seed.nodes.push_back({{"question", NodeKind::Question, "not detachable entity", {}}});
    seed.relations.push_back({{"a-exclusive", RelationKind::Contains, "archive-a", "exclusive"}});
    seed.relations.push_back({{"a-shared", RelationKind::Contains, "archive-a", "shared"}});
    seed.relations.push_back({{"b-shared", RelationKind::Contains, "archive-b", "shared"}});
    seed.relations.push_back({{"a-question", RelationKind::Contains, "archive-a", "question"}});
    CHECK(graph.apply(seed).committed);
    const auto plan = plan_cluster_archive_detach(graph, "archive-a",
        {"exclusive", "shared", "question", "missing", "exclusive"});
    CHECK(plan.exclusively_owned_entity_ids.size() == 1);
    CHECK(plan.exclusively_owned_entity_ids.front() == "exclusive");
    CHECK(plan.shared_member_ids.size() == 1);
    CHECK(plan.shared_member_ids.front() == "shared");
    CHECK(plan.ineligible_member_ids.size() == 2);
    CHECK(graph.nodes().count("exclusive") == 1);
    CHECK(graph.nodes().count("shared") == 1);
  }
  std::remove(graph_path.c_str());
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
  CHECK(!store.restore("cluster-a", 7, &record, &error, 13));
  CHECK(error == "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH");
  CHECK(store.restore("cluster-a", 7, &record, &error, 12));
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
  std::cout << "SURGE cluster archive store tests passed\n";
  return 0;
}
