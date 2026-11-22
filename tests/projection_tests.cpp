#include "surge/projection.hpp"
#include "surge/sha256.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <filesystem>

int main() {
  surge::ProjectionBatch batch;
  batch.projection_id = "projection-1";
  batch.idempotency_key = "projection-1-v1";
  batch.context_id = "context-1";
  batch.producer_id = "recognizer";
  batch.producer_version = "1.0";
  batch.policy_version = "ja-v1";
  batch.base_state_version = 7;
  batch.source_event_refs = {"event-1"};
  batch.evidence_refs = {"evidence-1"};
  batch.nodes = {
    {"cluster-1", surge::NodeKind::Cluster, "topic", {}},
    {"claim-1", surge::NodeKind::Claim, "meeting is Tuesday", {"evidence-1"}},
    {"evidence-1", surge::NodeKind::Evidence, "source span", {"event-1"}}
  };
  batch.memberships.push_back({"membership-1", "claim-1", "cluster-1", 0.82});
  batch.spatial_placements.push_back({"cluster-1", {1.0, 2.0, 3.0}});

  auto validation = surge::validate_projection_batch(batch);
  assert(validation.valid);

  surge::SemanticDelta delta;
  std::string error;
  assert(surge::build_projection_delta(batch, delta, error));
  assert(delta.delta_id == "projection-1");
  assert(delta.expected_state_version == 7);
  assert(delta.nodes.size() == 3);
  assert(delta.relations.size() == 1);
  assert(delta.relations.front().relation.kind == surge::RelationKind::Contains);
  assert(std::abs(delta.relations.front().relation.weight - 0.82) < 1e-12);
  assert(delta.cluster_position_updates.size() == 1);

  auto invalid = batch;
  invalid.memberships.front().upstream_weight = std::numeric_limits<double>::quiet_NaN();
  validation = surge::validate_projection_batch(invalid);
  assert(!validation.valid && validation.error == "NON_FINITE_MEMBERSHIP_WEIGHT");

  invalid = batch;
  invalid.evidence_refs = {"missing-evidence"};
  validation = surge::validate_projection_batch(invalid);
  assert(!validation.valid && validation.error == "EVIDENCE_NODE_NOT_FOUND");

  invalid = batch;
  invalid.spatial_placements.front().position.x = std::numeric_limits<double>::infinity();
  validation = surge::validate_projection_batch(invalid);
  assert(!validation.valid && validation.error == "INVALID_SPATIAL_PLACEMENT");

  invalid = batch;
  invalid.idempotency_key.clear();
  validation = surge::validate_projection_batch(invalid);
  assert(!validation.valid && validation.error == "MISSING_REQUIRED_METADATA");

  const auto journal_path =
      (std::filesystem::temp_directory_path() / "surge-projection-integration.sgj").string();
  std::filesystem::remove(journal_path);
  {
    surge::ContextGraph graph(journal_path);
    assert(graph.open());
    auto commit_batch = batch;
    commit_batch.base_state_version = 0;
    const auto committed = surge::apply_projection_batch(graph, commit_batch);
    assert(committed.committed && !committed.duplicate);
    assert(committed.state_version == 1);
    assert(graph.nodes().count("cluster-1") == 1);
    assert(graph.nodes().count("claim-1") == 1);
    assert(graph.nodes().count("evidence-1") == 1);
    assert(graph.relations().count("membership-1") == 1);

    const auto duplicate = surge::apply_projection_batch(graph, commit_batch);
    assert(duplicate.committed && duplicate.duplicate);
    assert(graph.state_version() == 1);
  }
  {
    surge::ContextGraph replayed(journal_path);
    assert(replayed.open());
    assert(replayed.state_version() == 1);
    assert(replayed.nodes().count("claim-1") == 1);
    assert(replayed.relations().count("membership-1") == 1);
  }
  std::filesystem::remove(journal_path);

  const auto atomic_path =
      (std::filesystem::temp_directory_path() / "surge-projection-atomic.sgj").string();
  std::filesystem::remove(atomic_path);
  const std::string source = "会議日程は火曜日";
  surge::ValidatedRecognitionPacket packet;
  packet.packet_id = "source-packet-1";
  packet.idempotency_key = "source-packet-idem-1";
  packet.context_id = "context-atomic";
  packet.source_id = "transcript-1";
  packet.source_generation = "session-1";
  packet.block_ref = "utterance-1";
  packet.offset_basis = "UTF8_BYTE_NFC";
  packet.start_offset = 0;
  packet.end_offset = source.size();
  packet.source_sequence = 1;
  packet.source_snapshot_digest = surge::sha256_hex(source);
  packet.source_range_digest = surge::sha256_hex(source);
  packet.canonical_request_digest = surge::sha256_hex("canonical-source-packet-1");

  auto atomic_batch = batch;
  atomic_batch.projection_id = "atomic-projection-1";
  atomic_batch.idempotency_key = "atomic-projection-idem-1";
  atomic_batch.context_id = packet.context_id;
  atomic_batch.base_state_version = 0;
  atomic_batch.source_event_refs = {packet.packet_id};
  atomic_batch.nodes[1].references = {"evidence-1"};
  atomic_batch.nodes[2].references = {packet.packet_id};

  {
    surge::EventJournal journal(atomic_path);
    assert(journal.open());
    surge::PacketAdmissionCoordinator packets(journal);
    assert(packets.recover());
    surge::ContextGraph graph(journal);
    assert(graph.open());

    const auto admitted = surge::apply_projection_batch(
        packets, graph, atomic_batch, packet, source);
    assert(admitted.admitted && !admitted.duplicate);
    assert(admitted.state_version == 1);
    assert(packets.packet_count() == 1);
    assert(graph.nodes().count("claim-1") == 1);
    assert(graph.nodes().count("evidence-1") == 1);
    assert(graph.relations().count("membership-1") == 1);

    const auto duplicate = surge::apply_projection_batch(
        packets, graph, atomic_batch, packet, source);
    assert(duplicate.admitted && duplicate.duplicate);
    assert(journal.state_version() == 1);

    auto mismatched = atomic_batch;
    mismatched.source_event_refs = {"another-packet"};
    const auto rejected = surge::apply_projection_batch(
        packets, graph, mismatched, packet, source);
    assert(!rejected.admitted);
    assert(rejected.error == "PROJECTION_SOURCE_PACKET_MISMATCH");
    assert(journal.state_version() == 1);
  }
  {
    surge::EventJournal journal(atomic_path);
    assert(journal.open());
    surge::PacketAdmissionCoordinator packets(journal);
    assert(packets.recover());
    surge::ContextGraph graph(journal);
    assert(graph.open());
    assert(packets.packet_count() == 1);
    assert(graph.state_version() == 1);
    assert(graph.nodes().count("claim-1") == 1);
    assert(graph.relations().count("membership-1") == 1);
  }
  std::filesystem::remove(atomic_path);
  return 0;
}
