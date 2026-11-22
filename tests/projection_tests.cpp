#include "test_support.hpp"
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
    {"evidence-1", surge::NodeKind::Evidence, "source span", {}}
  };
  batch.memberships.push_back({"membership-1", "claim-1", "cluster-1", 0.82});
  batch.spatial_placements.push_back({"cluster-1", {1.0, 2.0, 3.0}});

  auto validation = surge::validate_projection_batch(batch);
  CHECK(validation.valid);

  surge::SemanticDelta delta;
  std::string error;
  CHECK(surge::build_projection_delta(batch, delta, error));
  CHECK(delta.delta_id == "projection-1");
  CHECK(delta.expected_state_version == 7);
  CHECK(delta.nodes.size() == 3);
  CHECK(delta.nodes[1].node.kind == surge::NodeKind::Evidence);
  CHECK(delta.nodes[2].node.kind == surge::NodeKind::Claim);
  CHECK(delta.relations.size() == 1);
  CHECK(delta.relations.front().relation.kind == surge::RelationKind::Contains);
  CHECK(std::abs(delta.relations.front().relation.weight - 0.82) < 1e-12);
  CHECK(delta.cluster_position_updates.size() == 1);

  auto invalid = batch;
  invalid.memberships.front().upstream_weight = std::numeric_limits<double>::quiet_NaN();
  validation = surge::validate_projection_batch(invalid);
  CHECK(!validation.valid && validation.error == "NON_FINITE_MEMBERSHIP_WEIGHT");

  invalid = batch;
  invalid.evidence_refs = {"missing-evidence"};
  validation = surge::validate_projection_batch(invalid);
  CHECK(!validation.valid && validation.error == "EVIDENCE_NODE_NOT_FOUND");

  invalid = batch;
  invalid.spatial_placements.front().position.x = std::numeric_limits<double>::infinity();
  validation = surge::validate_projection_batch(invalid);
  CHECK(!validation.valid && validation.error == "INVALID_OR_DUPLICATE_SPATIAL_PLACEMENT");

  invalid = batch;
  invalid.source_event_refs.clear();
  validation = surge::validate_projection_batch(invalid);
  CHECK(!validation.valid && validation.error == "MISSING_SOURCE_OR_EVIDENCE");

  invalid = batch;
  invalid.nodes[0].references = {"claim-1"};
  invalid.nodes[1].references = {"cluster-1"};
  validation = surge::validate_projection_batch(invalid);
  CHECK(validation.valid);
  CHECK(!surge::build_projection_delta(invalid, delta, error));
  CHECK(error == "NODE_REFERENCE_CYCLE");

  invalid = batch;
  invalid.idempotency_key.clear();
  validation = surge::validate_projection_batch(invalid);
  CHECK(!validation.valid && validation.error == "MISSING_REQUIRED_METADATA");

  const auto journal_path =
      (std::filesystem::temp_directory_path() / "surge-projection-integration.sgj").string();
  std::filesystem::remove(journal_path);
  {
    surge::ContextGraph graph(journal_path);
    CHECK(graph.open());
    auto commit_batch = batch;
    commit_batch.base_state_version = 0;
    const auto committed = surge::apply_projection_batch(graph, commit_batch);
    CHECK(committed.committed && !committed.duplicate);
    CHECK(committed.state_version == 1);
    CHECK(graph.nodes().count("cluster-1") == 1);
    CHECK(graph.nodes().count("claim-1") == 1);
    CHECK(graph.nodes().count("evidence-1") == 1);
    CHECK(graph.relations().count("membership-1") == 1);

    const auto duplicate = surge::apply_projection_batch(graph, commit_batch);
    CHECK(duplicate.committed && duplicate.duplicate);
    CHECK(graph.state_version() == 1);
  }
  {
    surge::ContextGraph replayed(journal_path);
    CHECK(replayed.open());
    CHECK(replayed.state_version() == 1);
    CHECK(replayed.nodes().count("claim-1") == 1);
    CHECK(replayed.relations().count("membership-1") == 1);
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

  {
    surge::EventJournal journal(atomic_path);
    CHECK(journal.open());
    surge::PacketAdmissionCoordinator packets(journal);
    CHECK(packets.recover());
    surge::ContextGraph graph(journal);
    CHECK(graph.open());

    const auto admitted = surge::apply_projection_batch(
        packets, graph, atomic_batch, packet, source);
    CHECK(admitted.admitted && !admitted.duplicate);
    CHECK(admitted.state_version == 1);
    CHECK(packets.packet_count() == 1);
    CHECK(graph.nodes().count("claim-1") == 1);
    CHECK(graph.nodes().count("evidence-1") == 1);
    CHECK(graph.relations().count("membership-1") == 1);

    const auto duplicate = surge::apply_projection_batch(
        packets, graph, atomic_batch, packet, source);
    CHECK(duplicate.admitted && duplicate.duplicate);
    CHECK(journal.state_version() == 1);

    auto changed_projection = atomic_batch;
    changed_projection.nodes[1].content = "meeting-date=Wednesday";
    const auto changed = surge::apply_projection_batch(
        packets, graph, changed_projection, packet, source);
    CHECK(!changed.admitted);
    CHECK(changed.error == "IDEMPOTENCY_KEY_REUSED");
    CHECK(journal.state_version() == 1);

    auto second_packet = packet;
    second_packet.packet_id = "source-packet-2";
    second_packet.idempotency_key = "source-packet-idem-2";
    second_packet.block_ref = "utterance-2";
    second_packet.source_sequence = 2;
    auto reused_projection = atomic_batch;
    reused_projection.source_event_refs = {second_packet.packet_id};
    const auto reused = surge::apply_projection_batch(
        packets, graph, reused_projection, second_packet, source);
    CHECK(!reused.admitted);
    CHECK(reused.error == "PROJECTION_IDEMPOTENCY_KEY_REUSED");
    CHECK(journal.state_version() == 1);

    auto stale_packet = packet;
    stale_packet.packet_id = "source-packet-stale";
    stale_packet.idempotency_key = "source-packet-idem-stale";
    stale_packet.block_ref = "utterance-stale";
    stale_packet.source_sequence = 3;
    auto stale_batch = atomic_batch;
    stale_batch.projection_id = "stale-projection";
    stale_batch.idempotency_key = "stale-projection-idem";
    stale_batch.source_event_refs = {stale_packet.packet_id};
    stale_batch.base_state_version = 0;
    const auto stale = surge::apply_projection_batch(
        packets, graph, stale_batch, stale_packet, source);
    CHECK(!stale.admitted);
    CHECK(stale.error == "STATE_VERSION_CONFLICT");
    CHECK(journal.state_version() == 1);

    auto mismatched = atomic_batch;
    mismatched.source_event_refs = {"another-packet"};
    const auto rejected = surge::apply_projection_batch(
        packets, graph, mismatched, packet, source);
    CHECK(!rejected.admitted);
    CHECK(rejected.error == "PROJECTION_SOURCE_PACKET_MISMATCH");
    CHECK(journal.state_version() == 1);
  }
  {
    surge::EventJournal journal(atomic_path);
    CHECK(journal.open());
    surge::PacketAdmissionCoordinator packets(journal);
    CHECK(packets.recover());
    surge::ContextGraph graph(journal);
    CHECK(graph.open());
    CHECK(packets.packet_count() == 1);
    CHECK(graph.state_version() == 1);
    CHECK(graph.nodes().count("claim-1") == 1);
    CHECK(graph.relations().count("membership-1") == 1);
  }
  std::filesystem::remove(atomic_path);

  struct FormatCase { const char* id; const char* source; const char* claim; };
  const FormatCase formats[] = {
    {"conversation", "会議日程は火曜日", "meeting-date=Tuesday"},
    {"document", "第3条 申請期限は毎年3月末日とする。", "application-deadline=March-end"},
    {"structured", "asset=A-17;status=inspection_due", "asset:A-17/status=inspection_due"}
  };
  for (const auto& format : formats) {
    const std::string format_source = format.source;
    const std::string suffix = format.id;
    const auto format_path = (std::filesystem::temp_directory_path() /
        ("surge-projection-format-" + suffix + ".sgj")).string();
    std::filesystem::remove(format_path);
    auto format_packet = packet;
    format_packet.packet_id = "packet-" + suffix;
    format_packet.idempotency_key = "packet-idem-" + suffix;
    format_packet.context_id = "context-" + suffix;
    format_packet.source_id = "source-" + suffix;
    format_packet.block_ref = "block-" + suffix;
    format_packet.end_offset = format_source.size();
    format_packet.source_snapshot_digest = surge::sha256_hex(format_source);
    format_packet.source_range_digest = format_packet.source_snapshot_digest;
    format_packet.canonical_request_digest = surge::sha256_hex("canonical-" + suffix);

    auto format_batch = atomic_batch;
    format_batch.projection_id = "projection-" + suffix;
    format_batch.idempotency_key = "projection-idem-" + suffix;
    format_batch.context_id = format_packet.context_id;
    format_batch.base_state_version = 0;
    format_batch.source_event_refs = {format_packet.packet_id};
    format_batch.evidence_refs = {"evidence-" + suffix};
    format_batch.nodes = {
      {"cluster-" + suffix, surge::NodeKind::Cluster, "projected topic", {}},
      {"claim-" + suffix, surge::NodeKind::Claim, format.claim, {"evidence-" + suffix}},
      {"evidence-" + suffix, surge::NodeKind::Evidence, "source evidence", {}}
    };
    format_batch.memberships = {
      {"membership-" + suffix, "claim-" + suffix, "cluster-" + suffix, 0.82}
    };
    format_batch.spatial_placements = {
      {"cluster-" + suffix, {1.0, 2.0, 3.0}}
    };
    {
      surge::EventJournal journal(format_path);
      CHECK(journal.open());
      surge::PacketAdmissionCoordinator packets(journal);
      CHECK(packets.recover());
      surge::ContextGraph graph(journal);
      CHECK(graph.open());
      const auto result = surge::apply_projection_batch(
          packets, graph, format_batch, format_packet, format_source);
      CHECK(result.admitted && !result.duplicate);
      CHECK(result.state_version == 1);
      CHECK(graph.nodes().count("claim-" + suffix) == 1);
      CHECK(graph.nodes().count("evidence-" + suffix) == 1);
      CHECK(graph.relations().count("membership-" + suffix) == 1);
    }
    std::filesystem::remove(format_path);
  }
  return 0;
}
