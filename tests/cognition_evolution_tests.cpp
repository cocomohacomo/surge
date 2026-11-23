#include "test_support.hpp"
#include "surge/context_graph.hpp"
#include <filesystem>
#include <iostream>
using namespace surge;

int main() {
  const auto root = std::filesystem::temp_directory_path() / "surge-cognition-evolution-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  ContextGraph graph((root / "candidate-accumulation.sgj").string());
  CHECK(graph.open());

  SemanticDelta seed;
  seed.delta_id = "cognition-seed";
  seed.idempotency_key = "cognition-seed";
  seed.expected_state_version = 0;
  seed.nodes = {{{"evolution-cluster", NodeKind::Cluster, "river ecology", {}}},
                {{"evolution-member-1", NodeKind::Evidence, "river ecology seasonal habitat", {}}},
                {{"evolution-input", NodeKind::Evidence, "new river observation", {}}}};
  seed.relations = {{{"evolution-membership-1", RelationKind::Contains,
      "evolution-cluster", "evolution-member-1", 0.7}}};
  CHECK(graph.apply(seed).committed);

  const auto before = graph.recall_cognition_candidates("evolution-input", {"river"}, 8);
  CHECK(before.size() == 1);
  CHECK(before.front().cluster_id == "evolution-cluster");
  CHECK(before.front().supporting_member_ids.size() == 1);
  const auto recall_version = graph.state_version();

  SemanticDelta incoming;
  incoming.delta_id = "cognition-incoming-2";
  incoming.idempotency_key = "cognition-incoming-2";
  incoming.expected_state_version = graph.state_version();
  incoming.nodes = {{{"evolution-member-2", NodeKind::Evidence, "river ecology floodplain", {}}}};
  incoming.relations = {{{"evolution-membership-2", RelationKind::Contains,
      "evolution-cluster", "evolution-member-2", 0.4}}};
  CHECK(graph.apply(incoming).committed);

  const auto after = graph.recall_cognition_candidates("evolution-input", {"river"}, 8);
  CHECK(after.size() == 1);
  CHECK(after.front().cluster_id == "evolution-cluster");
  CHECK(after.front().supporting_member_ids.size() == 2);
  CHECK(after.front().recall_score >= before.front().recall_score);
  CHECK(after.front().memory_state_version == graph.state_version());
  CHECK(after.front().memory_state_version > recall_version);

  SemanticDelta snapshot_delta;
  snapshot_delta.delta_id = "cognition-state-snapshot-1";
  snapshot_delta.idempotency_key = "cognition-state-snapshot-1";
  snapshot_delta.expected_state_version = graph.state_version();
  std::string snapshot_error;
  CHECK(graph.append_cognition_state_snapshot(snapshot_delta, after.front(),
      "evolution-input", "evolution-snapshot-1", "evolution-snapshot-rel",
      snapshot_error));
  CHECK(snapshot_error.empty());
  CHECK(snapshot_delta.nodes.size() == 1);
  CHECK(snapshot_delta.relations.size() == 4);
  CHECK(snapshot_delta.nodes.front().node.content.find("SURGE_COGNITION_STATE_V1") == 0);
  CHECK(graph.apply(snapshot_delta).committed);
  CHECK(graph.nodes().count("evolution-snapshot-1") == 1);
  CHECK(graph.relations().count("evolution-snapshot-rel/cluster") == 1);
  CHECK(graph.relations().count("evolution-snapshot-rel/input") == 1);
  CHECK(graph.relations().count("evolution-snapshot-rel/support/0") == 1);
  CHECK(graph.relations().count("evolution-snapshot-rel/support/1") == 1);

  const auto stable_version = graph.state_version();
  const auto repeated = graph.recall_cognition_candidates("evolution-input", {"river"}, 8);
  CHECK(repeated.size() == after.size());
  CHECK(graph.state_version() == stable_version);
  CHECK(graph.observe_cognition().state_version == stable_version);

  std::filesystem::remove_all(root);
  std::cout << "SURGE cognition evolution: candidate accumulation and re-recall passed\n";
  return 0;
}
