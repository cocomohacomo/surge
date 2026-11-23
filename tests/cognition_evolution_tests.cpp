#include "test_support.hpp"
#include "surge/context_graph.hpp"
#include "surge/cognition_state_diff.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
using namespace surge;

int main() {
  const auto root = std::filesystem::temp_directory_path() / "surge-cognition-evolution-tests";

  // Co-occurrence is represented as a new cognition cluster. The external
  // Projector supplies the recognized content and source candidates; Core
  // only validates the source clusters and records DerivedFrom lineage.
  SemanticDelta cooccurrence_sources;
  cooccurrence_sources.delta_id = "cognition-cooccurrence-sources";
  cooccurrence_sources.idempotency_key = "cognition-cooccurrence-sources";
  cooccurrence_sources.expected_state_version = graph.state_version();
  cooccurrence_sources.nodes = {
      {{"evolution-cluster-2", NodeKind::Cluster, "tributary habitat", {}}},
      {{"evolution-member-3", NodeKind::Evidence, "tributary habitat seasonal flow", {}}},
      {{"evolution-member-4", NodeKind::Evidence, "tributary habitat floodplain", {}}}
  };
  cooccurrence_sources.relations = {
      {{"evolution-membership-3", RelationKind::Contains,
        "evolution-cluster", "evolution-member-3", 0.65}},
      {{"evolution-membership-4", RelationKind::Contains,
        "evolution-cluster-2", "evolution-member-4", 0.55}}
  };
  CHECK(graph.apply(cooccurrence_sources).committed);

  const auto cooccurrence_candidates = graph.recall_cognition_candidates(
      "evolution-input", {"tributary"}, 8);
  CHECK(cooccurrence_candidates.size() == 2);
  const auto source_one_before = graph.nodes().at("evolution-cluster").content;
  const auto source_two_before = graph.nodes().at("evolution-cluster-2").content;
  const auto source_one_memberships_before =
      graph.cluster_membership_reference_count("evolution-member-3");
  const auto source_two_memberships_before =
      graph.cluster_membership_reference_count("evolution-member-4");

  SemanticDelta cooccurrence_delta;
  cooccurrence_delta.delta_id = "cognition-cooccurrence-derived";
  cooccurrence_delta.idempotency_key = "cognition-cooccurrence-derived";
  cooccurrence_delta.expected_state_version = graph.state_version();
  std::string cooccurrence_error;
  CHECK(graph.append_derived_cognition_cluster(cooccurrence_delta,
      "evolution-cooccurrence-cluster",
      "Tributary flow and floodplain habitat form a coupled seasonal system",
      {"evolution-cluster", "evolution-cluster-2"},
      "evolution-cooccurrence-lineage", cooccurrence_error));
  CHECK(cooccurrence_error.empty());
  CHECK(cooccurrence_delta.nodes.size() == 1);
  CHECK(cooccurrence_delta.relations.size() == 2);
  CHECK(cooccurrence_delta.nodes.front().node.kind == NodeKind::Cluster);
  CHECK(graph.apply(cooccurrence_delta).committed);
  CHECK(graph.nodes().at("evolution-cooccurrence-cluster").content ==
      "Tributary flow and floodplain habitat form a coupled seasonal system");
  CHECK(graph.relations().count("evolution-cooccurrence-lineage/0") == 1);
  CHECK(graph.relations().count("evolution-cooccurrence-lineage/1") == 1);
  CHECK(graph.relations().at("evolution-cooccurrence-lineage/0").kind ==
      RelationKind::DerivedFrom);
  CHECK(graph.relations().at("evolution-cooccurrence-lineage/1").kind ==
      RelationKind::DerivedFrom);
  CHECK(graph.nodes().at("evolution-cluster").content == source_one_before);
  CHECK(graph.nodes().at("evolution-cluster-2").content == source_two_before);
  CHECK(graph.cluster_membership_reference_count("evolution-member-3") ==
      source_one_memberships_before);
  CHECK(graph.cluster_membership_reference_count("evolution-member-4") ==
      source_two_memberships_before);

  SemanticDelta invalid_cooccurrence;
  invalid_cooccurrence.delta_id = "cognition-cooccurrence-invalid";
  invalid_cooccurrence.idempotency_key = "cognition-cooccurrence-invalid";
  invalid_cooccurrence.expected_state_version = graph.state_version();
  CHECK(!graph.append_derived_cognition_cluster(invalid_cooccurrence,
      "evolution-invalid-derived", "invalid source set",
      {"evolution-cluster", "missing-source-cluster"},
      "evolution-invalid-lineage", cooccurrence_error));
  CHECK(cooccurrence_error == "DERIVED_COGNITION_SOURCE_NOT_FOUND");
  CHECK(invalid_cooccurrence.nodes.empty());
  CHECK(invalid_cooccurrence.relations.empty());

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
  CHECK(snapshot_delta.relations.size() == 2);
  CHECK(snapshot_delta.nodes.front().node.content.find("SURGE_COGNITION_STATE_V1") == 0);
  const auto snapshot_result = graph.apply(snapshot_delta);
  if (!snapshot_result.committed) std::cerr << "snapshot apply failed: " << snapshot_result.error << "\\n";
  CHECK(snapshot_result.committed);
  CHECK(graph.nodes().count("evolution-snapshot-1") == 1);
  CHECK(graph.relations().count("evolution-snapshot-rel/cluster") == 1);
  CHECK(graph.relations().count("evolution-snapshot-rel/membership") == 1);

  const auto stable_version = graph.state_version();
  const auto repeated = graph.recall_cognition_candidates("evolution-input", {"river"}, 8);
  CHECK(repeated.size() == after.size());
  CHECK(graph.state_version() == stable_version);
  CHECK(graph.observe_cognition().state_version == stable_version);

  // External evaluation is persisted as feedback, then applied only when the
  // same input is recalled again. It does not rewrite memberships or weights.
  SemanticDelta feedback_delta;
  feedback_delta.delta_id = "cognition-feedback-1";
  feedback_delta.idempotency_key = "cognition-feedback-1";
  feedback_delta.expected_state_version = graph.state_version();
  CognitionFeedback feedback;
  feedback.feedback_id = "external-review-1";
  feedback.candidate_cluster_id = "evolution-cluster";
  feedback.input_member_id = "evolution-input";
  feedback.outcome = "CONFIRMED";
  feedback.producer_id = "test-projector";
  feedback.producer_version = "1";
  feedback.policy_version = "test-policy-1";
  feedback.recorded_at = "2026-11-23T00:00:00Z";
  feedback.notes = "external evaluator confirmed this candidate";
  std::string feedback_error;
  CHECK(graph.append_cognition_feedback(feedback_delta, feedback,
      "evolution-feedback-1", "evolution-feedback-about", feedback_error));
  CHECK(feedback_error.empty());
  CHECK(graph.apply(feedback_delta).committed);

  const auto reevaluated = graph.recall_cognition_candidates(
      "evolution-input", {"river"}, 8);
  CHECK(reevaluated.size() == 1);
  CHECK(reevaluated.front().cluster_id == "evolution-cluster");
  CHECK(std::abs(reevaluated.front().recall_score -
      (repeated.front().recall_score + 0.20)) < 1e-12);
  CHECK(reevaluated.front().memory_state_version == graph.state_version());

  SemanticDelta reevaluated_snapshot_delta;
  reevaluated_snapshot_delta.delta_id = "cognition-state-snapshot-2";
  reevaluated_snapshot_delta.idempotency_key = "cognition-state-snapshot-2";
  reevaluated_snapshot_delta.expected_state_version = graph.state_version();
  CHECK(graph.append_cognition_state_snapshot(reevaluated_snapshot_delta,
      reevaluated.front(), "evolution-input", "evolution-snapshot-2",
      "evolution-snapshot-2-rel", snapshot_error));
  CHECK(graph.apply(reevaluated_snapshot_delta).committed);

  CognitionStateSnapshot first_snapshot, second_snapshot;
  CHECK(decode_cognition_state_snapshot(
      graph.nodes().at("evolution-snapshot-1"), first_snapshot, snapshot_error));
  CHECK(decode_cognition_state_snapshot(
      graph.nodes().at("evolution-snapshot-2"), second_snapshot, snapshot_error));
  CognitionStateDiff state_diff;
  CHECK(diff_cognition_state_snapshots(first_snapshot, second_snapshot,
      state_diff, snapshot_error));
  CHECK(std::abs(state_diff.recall_score_delta - 0.20) < 1e-12);
  CHECK(state_diff.cluster_id == "evolution-cluster");
  CHECK(state_diff.input_member_id == "evolution-input");

  std::filesystem::remove_all(root);
  std::cout << "SURGE cognition evolution: candidate accumulation and re-recall passed\n";
  return 0;
}
