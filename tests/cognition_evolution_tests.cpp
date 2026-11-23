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

  const auto stable_version = graph.state_version();
  const auto repeated = graph.recall_cognition_candidates("evolution-input", {"river"}, 8);
  CHECK(repeated.size() == after.size());
  CHECK(graph.state_version() == stable_version);
  CHECK(graph.observe_cognition().state_version == stable_version);

  std::filesystem::remove_all(root);
  std::cout << "SURGE cognition evolution: candidate accumulation and re-recall passed\n";
  return 0;
}
