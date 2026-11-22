#include "surge/c_api.h"
#include "surge/context_graph.hpp"
#include "surge/event_journal.hpp"
#include "surge/packet_admission.hpp"
#include "surge/sha256.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

int main() {
  const auto path = (std::filesystem::temp_directory_path() /
      "surge-projection-c-api.sgj").string();
  std::filesystem::remove(path);
  const std::string source = "会議日程は火曜日";
  const auto digest = surge::sha256_hex(source);

  char error[256]{};
  char status[64]{};
  auto* handle = surge_packet_core_open(path.c_str(), error, sizeof(error));
  assert(handle != nullptr);

  SurgeRecognitionPacketInput packet{};
  packet.packet_id = "packet-c-api-1";
  packet.idempotency_key = "packet-c-api-idem-1";
  packet.context_id = "context-c-api";
  packet.source_id = "transcript-c-api";
  packet.source_generation = "session-1";
  packet.block_ref = "utterance-1";
  packet.offset_basis = "UTF8_BYTE_NFC";
  packet.start_offset = 0;
  packet.end_offset = source.size();
  packet.source_sequence = 1;
  packet.source_snapshot_digest = digest.c_str();
  packet.source_range_digest = digest.c_str();
  packet.canonical_request_digest = "canonical-c-api-1";

  SurgeProjectionMetadataInput metadata{};
  metadata.projection_id = "projection-c-api-1";
  metadata.idempotency_key = "projection-c-api-idem-1";
  metadata.context_id = "context-c-api";
  metadata.producer_id = "fixture-projector";
  metadata.producer_version = "1.0";
  metadata.policy_version = "ja-v1";
  metadata.base_state_version = 0;

  const char* evidence_refs[] = {"evidence-c-api-1"};
  const char* claim_refs[] = {"evidence-c-api-1"};
  const char* evidence_source_refs[] = {"packet-c-api-1"};
  SurgeGraphNodeInput nodes[] = {
    {"cluster-c-api-1", "CLUSTER", "meeting plan", nullptr, 0},
    {"claim-c-api-1", "CLAIM", "meeting-date=Tuesday", claim_refs, 1},
    {"evidence-c-api-1", "EVIDENCE", "source utterance", evidence_source_refs, 1}
  };
  SurgeProjectionMembershipInput memberships[] = {
    {"membership-c-api-1", "claim-c-api-1", "cluster-c-api-1", 0.82}
  };
  SurgeProjectionSpatialPlacementInput placements[] = {
    {"cluster-c-api-1", 1.0, 2.0, 3.0}
  };
  int duplicate = 0;
  uint64_t state_version = 0;
  const auto* bytes = reinterpret_cast<const unsigned char*>(source.data());
  assert(surge_projection_batch_admit_and_apply(
      handle, &packet, bytes, source.size(), &metadata,
      evidence_refs, 1, nodes, 3, nullptr, 0, memberships, 1,
      placements, 1, &duplicate, &state_version, status, sizeof(status),
      error, sizeof(error)) == 1);
  assert(duplicate == 0 && state_version == 1);
  assert(std::string(status) == "COMMITTED");

  assert(surge_projection_batch_admit_and_apply(
      handle, &packet, bytes, source.size(), &metadata,
      evidence_refs, 1, nodes, 3, nullptr, 0, memberships, 1,
      placements, 1, &duplicate, &state_version, status, sizeof(status),
      error, sizeof(error)) == 1);
  assert(duplicate == 1 && state_version == 1);

  auto invalid_membership = memberships[0];
  invalid_membership.upstream_weight = std::numeric_limits<double>::quiet_NaN();
  assert(surge_projection_batch_admit_and_apply(
      handle, &packet, bytes, source.size(), &metadata,
      evidence_refs, 1, nodes, 3, nullptr, 0, &invalid_membership, 1,
      placements, 1, &duplicate, &state_version, status, sizeof(status),
      error, sizeof(error)) == 0);
  assert(std::string(error) == "NON_FINITE_MEMBERSHIP_WEIGHT");
  assert(state_version == 1);
  surge_packet_core_close(handle);

  {
    surge::EventJournal journal(path);
    assert(journal.open());
    surge::PacketAdmissionCoordinator packets(journal);
    assert(packets.recover() && packets.packet_count() == 1);
    surge::ContextGraph graph(journal);
    assert(graph.open());
    assert(graph.state_version() == 1);
    assert(graph.nodes().count("claim-c-api-1") == 1);
    assert(graph.relations().count("membership-c-api-1") == 1);
  }
  std::filesystem::remove(path);
  return 0;
}
