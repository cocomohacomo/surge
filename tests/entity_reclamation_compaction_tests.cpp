#include "surge/context_graph.hpp"
#include "surge/event_journal.hpp"
#include "surge/snapshot_coordinator.hpp"
#include "surge/snapshot_domains.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)

int main() {
  using namespace surge;
  const auto root = std::filesystem::temp_directory_path() / "surge_entity_compaction_tests";
  std::filesystem::create_directories(root);
  const auto journal_path = (root / "reclaim.sgj").string();
  const auto snapshot_path = (root / "reclaim.snapshot").string();
  const auto failed_journal_path = (root / "reclaim-failed.sgj").string();
  const auto failed_snapshot_path = (root / "reclaim-failed.snapshot").string();
  std::error_code ignored;
  for (const auto& path : {journal_path, snapshot_path, snapshot_path + ".bak",
                           snapshot_path + ".tmp", snapshot_path + ".bak.tmp",
                           failed_journal_path, failed_snapshot_path,
                           failed_snapshot_path + ".bak", failed_snapshot_path + ".tmp",
                           failed_snapshot_path + ".bak.tmp"})
    std::filesystem::remove(path, ignored);

  std::string error;
  {
    EventJournal journal(journal_path);
    ContextGraph graph(journal);
    CHECK(graph.open());

    SemanticDelta seed;
    seed.delta_id = "entity-seed";
    seed.idempotency_key = "entity-seed";
    seed.expected_state_version = 0;
    seed.nodes = {
      {{"retained-cluster", NodeKind::Cluster, "retained topic", {}}},
      {{"reclaimable-entity", NodeKind::Entity, "entity-payload-unique", {}}}
    };
    seed.relations = {
      {{"retained-membership", RelationKind::Contains, "retained-cluster", "reclaimable-entity"}}
    };
    const auto seeded = graph.apply(seed);
    CHECK(seeded.committed && seeded.state_version == 1);
    CHECK(graph.nodes().count("reclaimable-entity") == 1);

    SemanticDelta reclaim;
    reclaim.delta_id = "entity-reclaim";
    reclaim.idempotency_key = "entity-reclaim";
    reclaim.expected_state_version = 1;
    reclaim.remove_relation_ids = {"retained-membership"};
    reclaim.reclaim_entity_node_ids = {"reclaimable-entity"};
    const auto reclaimed = graph.apply(reclaim);
    CHECK(reclaimed.committed && reclaimed.state_version == 2);
    CHECK(graph.nodes().count("reclaimable-entity") == 0);
    CHECK(graph.nodes().count("retained-cluster") == 1);

    ContextGraphSnapshotDomain graph_domain(graph);
    SnapshotCoordinator coordinator;
    CHECK(coordinator.register_domain(&graph_domain, &error));
    SnapshotStore store(snapshot_path);
    CHECK(coordinator.compact(journal, store, "entity-reclaim-context", &error));
    CHECK(journal.checkpoint_info().compacted);
    CHECK(journal.checkpoint_info().base_state_version == 2);
    CHECK(journal.committed_events().empty());
    CHECK(std::filesystem::exists(journal_path + ".bak")); // rollback generation retains pre-compaction frames

    const auto stored = store.load();
    CHECK(stored.ok && !stored.used_backup);
    CHECK(stored.document.context_id == "entity-reclaim-context");
    CHECK(stored.document.state_version == 2);
    CHECK(stored.bytes.find("entity-payload-unique") == std::string::npos);
  }

  // A fresh process view must rebuild solely from the verified checkpoint.
  {
    EventJournal recovered_journal(journal_path);
    ContextGraph recovered_graph(recovered_journal);
    CHECK(recovered_graph.open());
    CHECK(recovered_journal.state_version() == 2);
    CHECK(recovered_journal.committed_events().empty());

    ContextGraphSnapshotDomain recovered_domain(recovered_graph);
    SnapshotCoordinator recovery;
    CHECK(recovery.register_domain(&recovered_domain, &error));
    SnapshotStore store(snapshot_path);
    CHECK(recovery.restore_from_store(store, recovered_journal,
        "entity-reclaim-context", 2, 2, JournalTailLimits{}, &error));
    CHECK(recovered_graph.state_version() == 2);
    CHECK(recovered_graph.nodes().count("reclaimable-entity") == 0);
    CHECK(recovered_graph.nodes().count("retained-cluster") == 1);
  }

  // Snapshot persistence failure must leave the original journal generation
  // and its replayable reclamation events intact.
  {
    EventJournal journal(failed_journal_path);
    ContextGraph graph(journal);
    CHECK(graph.open());
    SemanticDelta seed;
    seed.delta_id = "failed-seed";
    seed.idempotency_key = "failed-seed";
    seed.expected_state_version = 0;
    seed.nodes = {
      {{"failed-retained-cluster", NodeKind::Cluster, "retained", {}}},
      {{"failed-entity", NodeKind::Entity, "failed-entity-payload", {}}}
    };
    seed.relations = {
      {{"failed-membership", RelationKind::Contains, "failed-retained-cluster", "failed-entity"}}
    };
    CHECK(graph.apply(seed).committed);
    SemanticDelta reclaim;
    reclaim.delta_id = "failed-reclaim";
    reclaim.idempotency_key = "failed-reclaim";
    reclaim.expected_state_version = 1;
    reclaim.remove_relation_ids = {"failed-membership"};
    reclaim.reclaim_entity_node_ids = {"failed-entity"};
    CHECK(graph.apply(reclaim).committed);
    ContextGraphSnapshotDomain domain(graph);
    SnapshotCoordinator coordinator;
    CHECK(coordinator.register_domain(&domain, &error));
    SnapshotStoreLimits limits;
    limits.max_file_bytes = 1;
    SnapshotStore store(failed_snapshot_path, limits);
    CHECK(!coordinator.compact(journal, store, "failed-context", &error));
    CHECK(!journal.checkpoint_info().compacted);
    CHECK(journal.state_version() == 2);
    CHECK(journal.committed_events().size() > 0);
  }

  for (const auto& path : {journal_path, snapshot_path, snapshot_path + ".bak",
                           snapshot_path + ".tmp", snapshot_path + ".bak.tmp",
                           failed_journal_path, failed_snapshot_path,
                           failed_snapshot_path + ".bak", failed_snapshot_path + ".tmp",
                           failed_snapshot_path + ".bak.tmp"})
    std::filesystem::remove(path, ignored);
  std::filesystem::remove_all(root, ignored);
  std::cout << "SURGE Entity reclamation checkpoint compaction tests passed\n";
  return 0;
}
