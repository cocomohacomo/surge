#include "test_support.hpp"
#include "surge/projection.hpp"
#include "surge/sha256.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

class FixedProjectorDouble {
 public:
  surge::ProjectionBatch project(const std::string& id,
      std::uint64_t base_version, const std::string& event_id,
      const std::string& evidence_id, const std::string& cluster_id,
      const std::string& text, const std::string& claim_id,
      double weight) const {
    surge::ProjectionBatch batch;
    batch.projection_id = id;
    batch.idempotency_key = id + "-idem";
    batch.context_id = "fixed-projector-context";
    batch.producer_id = "fixed-projector-double";
    batch.producer_version = "1";
    batch.policy_version = "fixture-v1";
    batch.base_state_version = base_version;
    batch.source_event_refs = {event_id};
    batch.evidence_refs = {evidence_id};
    batch.nodes = {
      {cluster_id, surge::NodeKind::Cluster, "meeting schedule", {}},
      {evidence_id, surge::NodeKind::Evidence, text, {}},
      {claim_id, surge::NodeKind::Claim, text, {evidence_id}}
    };
    batch.memberships = {
      {id + "-membership-evidence", evidence_id, cluster_id, weight},
      {id + "-membership-claim", claim_id, cluster_id, weight}
    };
    batch.spatial_placements = {{cluster_id, {0.0, 0.0, 0.0}}};
    return batch;
  }
};

} // namespace

int main() {
  const auto path = (std::filesystem::temp_directory_path() /
      "surge-fixed-projector-core-e2e.sgj").string();
  std::filesystem::remove(path);
  const FixedProjectorDouble projector;
  surge::EventJournal journal(path);
  CHECK(journal.open());
  surge::PacketAdmissionCoordinator packets(journal);
  CHECK(packets.recover());
  surge::ContextGraph graph(journal);
  CHECK(graph.open());
  auto commit_projection = [&](const surge::ProjectionBatch& batch,
                               const std::string& source, std::uint64_t sequence) {
    surge::ValidatedRecognitionPacket packet;
    packet.packet_id = batch.source_event_refs.front();
    packet.idempotency_key = "packet-idem-" + packet.packet_id;
    packet.context_id = batch.context_id;
    packet.source_id = "fixed-projector-source";
    packet.source_generation = "fixture-session-1";
    packet.block_ref = packet.packet_id;
    packet.offset_basis = "UTF8_BYTE_NFC";
    packet.start_offset = 0;
    packet.end_offset = source.size();
    packet.source_sequence = sequence;
    packet.source_snapshot_digest = surge::sha256_hex(source);
    packet.source_range_digest = packet.source_snapshot_digest;
    packet.canonical_request_digest = surge::sha256_hex("canonical-" + packet.packet_id);
    return surge::apply_projection_batch(packets, graph, batch, packet, source);
  };

  // The projector is deterministic and fixed. It emits candidates, source
  // evidence, membership weights and placement; Core must preserve them.
  auto first = projector.project("fixed-projection-1", 0, "event-1",
      "evidence-tuesday", "cluster-meeting",
      "会議日程 meeting schedule Tuesday", "claim-tuesday", 0.73);
  auto invalid_projection = first;
  invalid_projection.producer_id.clear();
  const auto invalid_projection_result = commit_projection(
      invalid_projection, "会議日程 meeting schedule Tuesday", 1);
  CHECK(!invalid_projection_result.admitted);
  CHECK(invalid_projection_result.failure_stage == surge::AdmissionFailureStage::ProjectionValidation);
  CHECK(invalid_projection_result.projection_error == "MISSING_REQUIRED_METADATA");
  CHECK(invalid_projection.producer_id.empty());
  CHECK(first.producer_id == "fixed-projector-double");
  CHECK(graph.state_version() == 0 && packets.packet_count() == 0);

  const auto first_result = commit_projection(first, "会議日程 meeting schedule Tuesday", 1);
  CHECK(first_result.admitted && !first_result.duplicate);
  CHECK(first_result.failure_stage == surge::AdmissionFailureStage::None);
  CHECK(first_result.state_version == 1);
  CHECK(graph.nodes().at("evidence-tuesday").content ==
      "会議日程 meeting schedule Tuesday");
  CHECK(std::abs(graph.relations().at("fixed-projection-1-membership-evidence").weight
      - 0.73) < 1e-12);
  CHECK(graph.relations().at("fixed-projection-1-membership-evidence").kind ==
      surge::RelationKind::Contains);

  // A second independent topic cluster is also projector-supplied.
  surge::ProjectionBatch second_topic;
  second_topic.projection_id = "fixed-projection-topic-2";
  second_topic.idempotency_key = "fixed-projection-topic-2-idem";
  second_topic.context_id = "fixed-projector-context";
  second_topic.producer_id = "fixed-projector-double";
  second_topic.producer_version = "1";
  second_topic.policy_version = "fixture-v1";
  second_topic.base_state_version = graph.state_version();
  second_topic.source_event_refs = {"event-2"};
  second_topic.evidence_refs = {"evidence-deadline"};
  second_topic.nodes = {
    {"cluster-deadline", surge::NodeKind::Cluster, "project deadline", {}},
    {"evidence-deadline", surge::NodeKind::Evidence,
      "project deadline Friday", {}},
    {"claim-deadline", surge::NodeKind::Claim,
      "project deadline Friday", {"evidence-deadline"}}
  };
  second_topic.memberships = {
    {"topic2-evidence-member", "evidence-deadline", "cluster-deadline", 0.61},
    {"topic2-claim-member", "claim-deadline", "cluster-deadline", 0.61}
  };
  second_topic.spatial_placements = {{"cluster-deadline", {1.0, 0.0, 0.0}}};
  const auto second_result = commit_projection(second_topic, "project deadline Friday", 2);
  CHECK(second_result.admitted && second_result.state_version == 2);

  // Core recall is a structural/associative operation over projector-supplied
  // terms. It does not manufacture a Supports/AgreesWith relation.
  auto recall = graph.recall_cognition_candidates(
      "evidence-tuesday", {"meeting"}, 8);
  CHECK(std::any_of(recall.begin(), recall.end(), [](const auto& candidate) {
    return candidate.cluster_id == "cluster-meeting";
  }));
  CHECK(std::none_of(graph.relations().begin(), graph.relations().end(),
      [](const auto& item) {
        return item.second.kind == surge::RelationKind::AgreesWith ||
               item.second.kind == surge::RelationKind::Supports;
      }));

  // The fixed projector now emits a correction as a new Evidence/Claim,
  // explicit contradiction and unresolved Question. Original Tuesday
  // evidence remains immutable and present.
  surge::ProjectionBatch correction;
  correction.projection_id = "fixed-projection-correction";
  correction.idempotency_key = "fixed-projection-correction-idem";
  correction.context_id = "fixed-projector-context";
  correction.producer_id = "fixed-projector-double";
  correction.producer_version = "1";
  correction.policy_version = "fixture-v1";
  correction.base_state_version = graph.state_version();
  correction.source_event_refs = {"event-3"};
  correction.evidence_refs = {"evidence-wednesday"};
  correction.nodes = {
    {"evidence-wednesday", surge::NodeKind::Evidence,
      "会議日程 meeting schedule Wednesday", {}},
    {"claim-wednesday", surge::NodeKind::Claim,
      "meeting schedule Wednesday", {"evidence-wednesday"}},
    {"question-date", surge::NodeKind::Question,
      "Which meeting date is applicable?", {}},
    {"question-aspect-date", surge::NodeKind::QuestionAspect,
      "meeting date", {"question-date"}},
    {"interest-date", surge::NodeKind::Interest,
      "meeting date discrepancy", {"question-aspect-date", "cluster-meeting"}}
  };
  correction.relations = {
    {"relation-date-conflict", surge::RelationKind::Contradicts,
      "claim-tuesday", "claim-wednesday", 0.88},
    {"relation-question-tuesday", surge::RelationKind::About,
      "question-date", "claim-tuesday", 1.0},
    {"relation-question-wednesday", surge::RelationKind::About,
      "question-date", "claim-wednesday", 1.0},
    {"relation-question-interest", surge::RelationKind::About,
      "interest-date", "question-date", 1.0}
  };
  correction.memberships = {
    {"correction-evidence-member", "evidence-wednesday", "cluster-meeting", 0.91},
    {"correction-claim-member", "claim-wednesday", "cluster-meeting", 0.91},
    {"correction-question-member", "question-date", "cluster-meeting", 0.80},
    {"correction-question-aspect-member", "question-aspect-date", "cluster-meeting", 0.80},
    {"correction-interest-member", "interest-date", "cluster-meeting", 0.80}
  };
  const auto correction_result = commit_projection(correction, "会議日程 meeting schedule Wednesday", 3);
  if (!correction_result.admitted) std::cerr << "correction admission rejected: " << correction_result.error << "\\n";
  CHECK(correction_result.admitted);
  CHECK(correction_result.failure_stage == surge::AdmissionFailureStage::None);
  CHECK(graph.nodes().count("evidence-tuesday") == 1);
  CHECK(packets.packet_count() == 3);
  CHECK(graph.nodes().count("evidence-wednesday") == 1);
  CHECK(graph.relations().at("relation-date-conflict").kind ==
      surge::RelationKind::Contradicts);
  CHECK(graph.relations().at("correction-evidence-member").weight == 0.91);

  const auto after_correction = graph.recall_cognition_candidates(
      "evidence-tuesday", {"meeting"}, 8);
  CHECK(std::any_of(after_correction.begin(), after_correction.end(),
      [](const auto& candidate) {
        return candidate.cluster_id == "cluster-meeting" &&
               candidate.supporting_member_ids.size() >= 2;
      }));

  // Core records a new co-occurrence cluster from the two projector-created
  // source clusters. It does not invent its semantic label or lineage sources.
  surge::SemanticDelta cooccurrence;
  cooccurrence.delta_id = "core-cooccurrence-cluster";
  cooccurrence.idempotency_key = cooccurrence.delta_id;
  cooccurrence.expected_state_version = graph.state_version();
  std::string error;
  CHECK(graph.append_derived_cognition_cluster(cooccurrence,
      "cluster-meeting-deadline",
      "meeting schedule and project deadline co-occurrence",
      {"cluster-meeting", "cluster-deadline"},
      "cooccurrence-lineage", error));
  CHECK(graph.apply(cooccurrence).committed);
  CHECK(graph.relations().at("cooccurrence-lineage/0").kind ==
      surge::RelationKind::DerivedFrom);
  CHECK(graph.relations().at("cooccurrence-lineage/1").kind ==
      surge::RelationKind::DerivedFrom);
  const auto derived = graph.recall_cognition_candidates(
      "evidence-tuesday", {"meeting"}, 16);
  CHECK(std::any_of(derived.begin(), derived.end(), [](const auto& candidate) {
    return candidate.cluster_id == "cluster-meeting-deadline";
  }));

  // External evaluation feedback changes only recall ranking. The upstream
  // membership weight remains exactly the value emitted by the projector.
  const auto before_feedback = graph.recall_cognition_candidates(
      "evidence-tuesday", {"meeting"}, 16);
  const auto candidate_before = std::find_if(before_feedback.begin(),
      before_feedback.end(), [](const auto& candidate) {
        return candidate.cluster_id == "cluster-meeting";
      });
  CHECK(candidate_before != before_feedback.end());
  surge::CognitionFeedback feedback;
  feedback.feedback_id = "external-confirmation";
  feedback.candidate_cluster_id = "cluster-meeting";
  feedback.input_member_id = "evidence-tuesday";
  feedback.outcome = "CONFIRMED";
  feedback.producer_id = "external-reviewer";
  feedback.producer_version = "1";
  feedback.policy_version = "review-v1";
  feedback.recorded_at = "2026-11-24T00:00:00Z";
  feedback.notes = "fixed test oracle confirms candidate";
  surge::SemanticDelta feedback_delta;
  feedback_delta.delta_id = "external-feedback-delta";
  feedback_delta.idempotency_key = feedback_delta.delta_id;
  feedback_delta.expected_state_version = graph.state_version();
  CHECK(graph.append_cognition_feedback(feedback_delta, feedback,
      "external-feedback-node", "external-feedback-relation", error));
  CHECK(graph.apply(feedback_delta).committed);
  const auto after_feedback = graph.recall_cognition_candidates(
      "evidence-tuesday", {"meeting"}, 16);
  const auto candidate_after = std::find_if(after_feedback.begin(),
      after_feedback.end(), [](const auto& candidate) {
        return candidate.cluster_id == "cluster-meeting";
      });
  CHECK(candidate_after != after_feedback.end());
  CHECK(candidate_after->recall_score > candidate_before->recall_score);
  CHECK(graph.relations().at("fixed-projection-1-membership-evidence").weight == 0.73);

  // Focus observes the graph state without changing it. Both roots are in the
  // region; the observation reports the newly derived cognition cluster.
  const surge::FocusRegion region{{0.0, 0.0, 0.0}, {5.0, 5.0, 5.0}};
  const std::vector<surge::ClusterSpatialPosition> positions = {
    {"cluster-meeting", {0.0, 0.0, 0.0}},
    {"cluster-deadline", {1.0, 0.0, 0.0}},
    {"cluster-meeting-deadline", {0.5, 0.0, 0.0}}
  };
  const std::vector<surge::ClusterFocusDepth> depths = {
    {"cluster-meeting", 2}, {"cluster-deadline", 1},
    {"cluster-meeting-deadline", 1}
  };
  const auto version_before_observation = graph.state_version();
  const auto observation = graph.observe_focused_cognition(
      region, positions, depths, 256, 512);
  CHECK(observation.complete);
  CHECK(std::find(observation.logical_region_cluster_ids.begin(),
      observation.logical_region_cluster_ids.end(), "cluster-meeting") !=
      observation.logical_region_cluster_ids.end());
  CHECK(graph.state_version() == version_before_observation);

  std::filesystem::remove(path);
  return 0;
}
