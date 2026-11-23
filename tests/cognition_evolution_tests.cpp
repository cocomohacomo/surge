#include "test_support.hpp"
#include "surge/context_graph.hpp"
#include "surge/budgeted_context_graph.hpp"
#include "surge/cognition_state_diff.hpp"
#include <algorithm>
#include <cmath>
#include <set>
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

  // The Focus region is a persisted logical boundary. Placement of the
  // derived cluster is supplied explicitly by the external Projector below.
  std::string cooccurrence_error;
  SemanticDelta cooccurrence_focus_region;
  cooccurrence_focus_region.delta_id = "cognition-cooccurrence-focus-region";
  cooccurrence_focus_region.idempotency_key = "cognition-cooccurrence-focus-region";
  cooccurrence_focus_region.expected_state_version = graph.state_version();
  CHECK(graph.append_focus_region_update(cooccurrence_focus_region,
      FocusRegion{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}},
      cooccurrence_error));
  CHECK(graph.apply(cooccurrence_focus_region).committed);

  SemanticDelta cooccurrence_delta;
  cooccurrence_delta.delta_id = "cognition-cooccurrence-derived";
  cooccurrence_delta.idempotency_key = "cognition-cooccurrence-derived";
  cooccurrence_delta.expected_state_version = graph.state_version();
  CHECK(graph.append_derived_cognition_cluster(cooccurrence_delta,
      "evolution-cooccurrence-cluster",
      "Tributary flow and floodplain habitat form a coupled seasonal system",
      {"evolution-cluster", "evolution-cluster-2"},
      "evolution-cooccurrence-lineage", cooccurrence_error));
  CHECK(cooccurrence_error.empty());
  CHECK(cooccurrence_delta.nodes.size() == 1);
  CHECK(cooccurrence_delta.relations.size() == 2);
  CHECK(cooccurrence_delta.nodes.front().node.kind == NodeKind::Cluster);
  // External Projector placement and per-cluster depth are committed in the
  // same delta as cluster creation. Core does not derive these coordinates.
  CHECK(graph.append_cluster_spatial_position_update(cooccurrence_delta,
      {"evolution-cooccurrence-cluster", {0.25, 0.0, 0.0}},
      cooccurrence_error));
  CHECK(graph.append_cluster_focus_depth_update(cooccurrence_delta,
      {"evolution-cooccurrence-cluster", 0}, cooccurrence_error));
  CHECK(graph.apply(cooccurrence_delta).committed);
  CHECK(graph.nodes().at("evolution-cooccurrence-cluster").content ==
      "Tributary flow and floodplain habitat form a coupled seasonal system");
  CHECK(graph.relations().count("evolution-cooccurrence-lineage/0") == 1);
  CHECK(graph.relations().count("evolution-cooccurrence-lineage/1") == 1);
  CHECK(graph.relations().at("evolution-cooccurrence-lineage/0").kind ==
      RelationKind::DerivedFrom);
  CHECK(graph.relations().at("evolution-cooccurrence-lineage/1").kind ==
      RelationKind::DerivedFrom);
  CHECK(graph.relations().at("evolution-cooccurrence-lineage/0").from ==
      "evolution-cooccurrence-cluster");
  CHECK(graph.relations().at("evolution-cooccurrence-lineage/1").from ==
      "evolution-cooccurrence-cluster");
  const std::set<std::string> lineage_targets = {
      graph.relations().at("evolution-cooccurrence-lineage/0").to,
      graph.relations().at("evolution-cooccurrence-lineage/1").to};
  CHECK(lineage_targets == std::set<std::string>({
      "evolution-cluster", "evolution-cluster-2"}));
  // Core did not invent these coordinates: the explicit Projector update
  // above is the sole source of the persisted spatial placement.
  CHECK(graph.cluster_membership_reference_count(
      "evolution-cooccurrence-cluster") == 0);
  const auto focused_cooccurrence = graph.observe_focused_cognition(16, 32);
  CHECK(focused_cooccurrence.focus_region.has_value());
  CHECK(focused_cooccurrence.logical_region_cluster_ids ==
      std::vector<std::string>({"evolution-cooccurrence-cluster"}));
  CHECK(focused_cooccurrence.processed_region_cluster_ids ==
      std::vector<std::string>({"evolution-cooccurrence-cluster"}));
  CHECK(focused_cooccurrence.deferred_region_cluster_ids.empty());
  CHECK(std::any_of(focused_cooccurrence.nodes.begin(),
      focused_cooccurrence.nodes.end(), [](const GraphNode& node) {
        return node.id == "evolution-cooccurrence-cluster";
      }));
  const auto persisted_cognition = graph.observe_cognition();
  CHECK(std::any_of(persisted_cognition.cluster_spatial_positions.begin(),
      persisted_cognition.cluster_spatial_positions.end(),
      [](const ClusterSpatialPosition& item) {
        return item.cluster_id == "evolution-cooccurrence-cluster" &&
            item.position.x == 0.25;
      }));
  CHECK(std::any_of(persisted_cognition.cluster_focus_depths.begin(),
      persisted_cognition.cluster_focus_depths.end(),
      [](const ClusterFocusDepth& item) {
        return item.cluster_id == "evolution-cooccurrence-cluster" &&
            item.depth == 0;
      }));

  // A generated cognition cluster must re-enter ordinary candidate recall.
  // Cluster content is an independent activation path; no synthetic Evidence
  // member or Core-assigned classification/weight is created for it.
  const auto derived_recall_before = graph.recall_cognition_candidates(
      "evolution-input", {"coupled"}, 8);
  const auto derived_candidate_before = std::find_if(
      derived_recall_before.begin(), derived_recall_before.end(),
      [](const CognitionCandidate& candidate) {
        return candidate.cluster_id == "evolution-cooccurrence-cluster";
      });
  CHECK(derived_candidate_before != derived_recall_before.end());
  CHECK(derived_candidate_before->supporting_member_ids.empty());

  SemanticDelta derived_snapshot_before_delta;
  derived_snapshot_before_delta.delta_id = "derived-cognition-snapshot-before";
  derived_snapshot_before_delta.idempotency_key = "derived-cognition-snapshot-before";
  derived_snapshot_before_delta.expected_state_version = graph.state_version();
  CHECK(graph.append_cognition_state_snapshot(derived_snapshot_before_delta,
      *derived_candidate_before, "evolution-input",
      "evolution-derived-snapshot-before", "evolution-derived-snapshot-before-rel",
      cooccurrence_error));
  CHECK(graph.apply(derived_snapshot_before_delta).committed);

  // Recursive co-occurrence: the newly generated cluster participates as a
  // source alongside a second existing cluster. The external Projector also
  // supplies the new cluster's spatial placement and Focus depth.
  SemanticDelta recursive_cooccurrence;
  recursive_cooccurrence.delta_id = "cognition-recursive-cooccurrence";
  recursive_cooccurrence.idempotency_key = "cognition-recursive-cooccurrence";
  recursive_cooccurrence.expected_state_version = graph.state_version();
  CHECK(graph.append_derived_cognition_cluster(recursive_cooccurrence,
      "evolution-recursive-cluster",
      "coupled tributary habitat and seasonal floodplain dynamics",
      {"evolution-cooccurrence-cluster", "evolution-cluster-2"},
      "evolution-recursive-lineage", cooccurrence_error));
  CHECK(graph.append_cluster_spatial_position_update(recursive_cooccurrence,
      {"evolution-recursive-cluster", {0.5, 0.0, 0.0}}, cooccurrence_error));
  CHECK(graph.append_cluster_focus_depth_update(recursive_cooccurrence,
      {"evolution-recursive-cluster", 0}, cooccurrence_error));
  CHECK(graph.apply(recursive_cooccurrence).committed);
  CHECK(graph.relations().at("evolution-recursive-lineage/0").kind ==
      RelationKind::DerivedFrom);
  CHECK(graph.relations().at("evolution-recursive-lineage/1").kind ==
      RelationKind::DerivedFrom);

  const auto recursive_recall = graph.recall_cognition_candidates(
      "evolution-input", {"coupled"}, 8);
  const auto recursive_candidate = std::find_if(recursive_recall.begin(),
      recursive_recall.end(), [](const CognitionCandidate& candidate) {
        return candidate.cluster_id == "evolution-recursive-cluster";
      });
  CHECK(recursive_candidate != recursive_recall.end());
  const auto derived_recall_after = std::find_if(recursive_recall.begin(),
      recursive_recall.end(), [](const CognitionCandidate& candidate) {
        return candidate.cluster_id == "evolution-cooccurrence-cluster";
      });
  CHECK(derived_recall_after != recursive_recall.end());

  SemanticDelta derived_snapshot_after_delta;
  derived_snapshot_after_delta.delta_id = "derived-cognition-snapshot-after";
  derived_snapshot_after_delta.idempotency_key = "derived-cognition-snapshot-after";
  derived_snapshot_after_delta.expected_state_version = graph.state_version();
  CHECK(graph.append_cognition_state_snapshot(derived_snapshot_after_delta,
      *derived_recall_after, "evolution-input",
      "evolution-derived-snapshot-after", "evolution-derived-snapshot-after-rel",
      cooccurrence_error));
  CHECK(graph.apply(derived_snapshot_after_delta).committed);

  CognitionStateSnapshot derived_before_snapshot, derived_after_snapshot;
  CHECK(decode_cognition_state_snapshot(
      graph.nodes().at("evolution-derived-snapshot-before"),
      derived_before_snapshot, cooccurrence_error));
  CHECK(decode_cognition_state_snapshot(
      graph.nodes().at("evolution-derived-snapshot-after"),
      derived_after_snapshot, cooccurrence_error));
  CognitionStateDiff derived_history_diff;
  CHECK(diff_cognition_state_snapshots(derived_before_snapshot,
      derived_after_snapshot, derived_history_diff, cooccurrence_error));
  CHECK(derived_history_diff.cluster_id == "evolution-cooccurrence-cluster");
  CHECK(derived_history_diff.current_state_version >
      derived_history_diff.previous_state_version);
  // The recursive cluster is now a second matching seed. Its DerivedFrom
  // edge contributes the existing one-hop association factor (0.25 * 0.30)
  // to the generated cluster; the snapshot must preserve that state change.
  CHECK(std::abs(derived_history_diff.recall_score_delta - 0.075) < 1e-12);

  const auto focused_recursive = graph.observe_focused_cognition(16, 32);
  CHECK(focused_recursive.logical_region_cluster_ids ==
      std::vector<std::string>({
          "evolution-cooccurrence-cluster", "evolution-recursive-cluster"}));
  CHECK(focused_recursive.processed_region_cluster_ids ==
      focused_recursive.logical_region_cluster_ids);
  CHECK(focused_recursive.deferred_region_cluster_ids.empty());

  // Moving the recursive cluster outside the logical Focus region changes the
  // next observation without shrinking or rewriting the Focus region itself.
  SemanticDelta move_recursive_out;
  move_recursive_out.delta_id = "move-recursive-cluster-out-of-focus";
  move_recursive_out.idempotency_key = "move-recursive-cluster-out-of-focus";
  move_recursive_out.expected_state_version = graph.state_version();
  CHECK(graph.append_cluster_spatial_position_update(move_recursive_out,
      {"evolution-recursive-cluster", {2.0, 0.0, 0.0}}, cooccurrence_error));
  CHECK(graph.apply(move_recursive_out).committed);
  const auto focused_after_move = graph.observe_focused_cognition(16, 32);
  CHECK(focused_after_move.logical_region_cluster_ids ==
      std::vector<std::string>({"evolution-cooccurrence-cluster"}));
  CHECK(focused_after_move.focus_region.has_value());
  CHECK(focused_after_move.focus_region->half_extents.x == 1.0);

  // Restore the external placement and independently change per-cluster depth.
  SemanticDelta restore_recursive_focus;
  restore_recursive_focus.delta_id = "restore-recursive-cluster-focus";
  restore_recursive_focus.idempotency_key = "restore-recursive-cluster-focus";
  restore_recursive_focus.expected_state_version = graph.state_version();
  CHECK(graph.append_cluster_spatial_position_update(restore_recursive_focus,
      {"evolution-recursive-cluster", {0.5, 0.0, 0.0}}, cooccurrence_error));
  CHECK(graph.append_cluster_focus_depth_update(restore_recursive_focus,
      {"evolution-recursive-cluster", 1}, cooccurrence_error));
  CHECK(graph.apply(restore_recursive_focus).committed);
  const auto focused_after_depth_change = graph.observe_focused_cognition(16, 32);
  CHECK(focused_after_depth_change.logical_region_cluster_ids.size() == 2);
  const auto depth_observation = graph.observe_cognition();
  CHECK(std::any_of(depth_observation.cluster_focus_depths.begin(),
      depth_observation.cluster_focus_depths.end(),
      [](const ClusterFocusDepth& item) {
        return item.cluster_id == "evolution-recursive-cluster" &&
            item.depth == 1;
      }));
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


  // The BudgetedContextGraph facade commits derived cognition through the
  // same admission gate as ordinary semantic deltas.
  {
    ContextGraph budget_graph((root / "budgeted-derived.sgj").string());
    CHECK(budget_graph.open());
    ResourceBudgetLimits limits;
    limits.resident_bytes = 1U << 20;
    limits.transient_bytes = 1U << 20;
    limits.retained_events = 64;
    limits.retained_clusters = 16;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(budget_graph, budget, 1U << 20);
    std::string budget_error;
    CHECK(guarded.recover(budget_error));

    SemanticDelta sources;
    sources.delta_id = "budgeted-derived-sources";
    sources.idempotency_key = "budgeted-derived-sources";
    sources.expected_state_version = budget_graph.state_version();
    sources.nodes = {
        {{"budget-source-a", NodeKind::Cluster, "source A", {}}},
        {{"budget-source-b", NodeKind::Cluster, "source B", {}}}
    };
    CHECK(guarded.apply(sources).committed);

    SemanticDelta derived;
    derived.delta_id = "budgeted-derived-cluster";
    derived.idempotency_key = "budgeted-derived-cluster";
    derived.expected_state_version = budget_graph.state_version();
    const auto committed = guarded.append_derived_cognition_cluster(derived,
        "budget-derived", "externally projected co-occurrence",
        {"budget-source-a", "budget-source-b"}, "budget-derived-lineage",
        budget_error);
    CHECK(committed.committed);
    CHECK(budget_error.empty());
    CHECK(budget_graph.nodes().count("budget-derived") == 1);
    CHECK(budget_graph.relations().count("budget-derived-lineage/0") == 1);
    CHECK(budget_graph.relations().count("budget-derived-lineage/1") == 1);
  }

  std::filesystem::remove_all(root);
  std::cout << "SURGE cognition evolution: candidate accumulation and re-recall passed\n";
  return 0;
}
