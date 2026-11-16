#include "surge/c_api.h"
#include "surge/sha256.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using surge::sha256_hex;

int main() {
  const std::string source = "text";
  const std::string proposal_json =
      R"({"proposal_id":"proposal-1","packet_ref":"packet-1","schema_version":"0.9.0"})";
  const auto root = std::filesystem::temp_directory_path() /
      "surge-classification-proposal-journal-tests";
  std::filesystem::remove_all(root);
  const auto path = (root / "proposal.sgj").string();

  SurgeRecognitionPacketInput packet{};
  packet.packet_id = "packet-1";
  packet.idempotency_key = "packet-idem-1";
  packet.context_id = "context-1";
  packet.source_id = "source-1";
  packet.source_generation = "generation-1";
  packet.block_ref = "block-1";
  packet.offset_basis = "UTF8_BYTE_NFC";
  packet.start_offset = 0;
  packet.end_offset = source.size();
  packet.source_sequence = 1;
  const auto digest = sha256_hex(source);
  packet.source_snapshot_digest = digest.c_str();
  packet.source_range_digest = digest.c_str();
  const std::string request_digest = sha256_hex("canonical-packet");
  packet.canonical_request_digest = request_digest.c_str();
  packet.revision = 0;

  const std::string node_id = "evidence-1";
  const std::string node_kind = "EVIDENCE";
  const std::string node_content = "text evidence";
  SurgeGraphNodeInput node{node_id.c_str(), node_kind.c_str(),
                           node_content.c_str(), nullptr, 0};
  const std::string proposal_id = "proposal-1";
  const std::string packet_ref = "packet-1";
  const std::string generation = "generation-1";
  SurgeClassificationProposalInput proposal{
      proposal_id.c_str(), packet_ref.c_str(), 0, 0, generation.c_str(), 1,
      proposal_json.c_str()};

  {
    char error[512]{};
    void* handle = surge_packet_core_open(path.c_str(), error, sizeof(error));
    assert(handle != nullptr);
    int duplicate = 0;
    std::uint64_t version = 0;
    char status[128]{};
    const int committed = surge_packet_core_admit_and_apply_with_proposal(
        handle, &packet,
        reinterpret_cast<const unsigned char*>(source.data()), source.size(),
        "delta-1", "semantic-idem-1", 0,
        &node, 1, nullptr, 0, nullptr, 0, nullptr, nullptr,
        &proposal, &duplicate, &version, status, sizeof(status),
        error, sizeof(error));
    assert(committed == 1);
    assert(version == 1);
    assert(duplicate == 0);
    surge_packet_core_close(handle);
  }

  // The exact serialized proposal remains retrievable after a fresh Core open.
  {
    char error[512]{};
    void* handle = surge_packet_core_open(path.c_str(), error, sizeof(error));
    assert(handle != nullptr);
    std::size_t required = 0;
    const int size_query = surge_classification_proposal_get(
        handle, proposal_id.c_str(), nullptr, 0, &required, error, sizeof(error));
    assert(size_query == 0);
    assert(std::string(error) == "CLASSIFICATION_PROPOSAL_BUFFER_TOO_SMALL");
    assert(required == proposal_json.size() + 1);
    std::string output(required, '\0');
    const int loaded = surge_classification_proposal_get(
        handle, proposal_id.c_str(), output.data(), output.size(),
        &required, error, sizeof(error));
    assert(loaded == 1);
    assert(output == proposal_json);
    surge_packet_core_close(handle);
  }

  // A proposal with stale or mismatched lineage must not partially commit.
  {
    const auto reject_path = (root / "reject.sgj").string();
    char error[512]{};
    void* handle = surge_packet_core_open(reject_path.c_str(), error, sizeof(error));
    assert(handle != nullptr);
    SurgeClassificationProposalInput stale = proposal;
    stale.base_state_version = 7;
    int duplicate = 0;
    std::uint64_t version = 99;
    char status[128]{};
    const int committed = surge_packet_core_admit_and_apply_with_proposal(
        handle, &packet,
        reinterpret_cast<const unsigned char*>(source.data()), source.size(),
        "delta-1", "semantic-idem-1", 0,
        &node, 1, nullptr, 0, nullptr, 0, nullptr, nullptr,
        &stale, &duplicate, &version, status, sizeof(status),
        error, sizeof(error));
    assert(committed == 0);
    assert(std::string(error) == "CLASSIFICATION_PROPOSAL_LINEAGE_INVALID");
    assert(surge_packet_core_state_version(handle) == 0);
    surge_packet_core_close(handle);
  }

  // The original ABI remains available and commits without proposal metadata.
  {
    const auto legacy_path = (root / "legacy.sgj").string();
    char error[512]{};
    void* handle = surge_packet_core_open(legacy_path.c_str(), error, sizeof(error));
    assert(handle != nullptr);
    SurgeRecognitionPacketInput legacy_packet = packet;
    legacy_packet.packet_id = "packet-legacy";
    legacy_packet.idempotency_key = "packet-idem-legacy";
    const std::string legacy_request_digest = sha256_hex("legacy-canonical");
    legacy_packet.canonical_request_digest = legacy_request_digest.c_str();
    const std::string legacy_node_id = "evidence-legacy";
    const std::string legacy_node_content = "legacy evidence";
    SurgeGraphNodeInput legacy_node{legacy_node_id.c_str(), node_kind.c_str(),
                                    legacy_node_content.c_str(), nullptr, 0};
    int duplicate = 0;
    std::uint64_t version = 0;
    char status[128]{};
    const int committed = surge_packet_core_admit_and_apply(
        handle, &legacy_packet,
        reinterpret_cast<const unsigned char*>(source.data()), source.size(),
        "delta-legacy", "semantic-idem-legacy", 0,
        &legacy_node, 1, nullptr, 0, nullptr, 0, nullptr, nullptr,
        &duplicate, &version, status, sizeof(status), error, sizeof(error));
    assert(committed == 1);
    assert(version == 1);
    surge_packet_core_close(handle);
  }

  std::filesystem::remove_all(root);
  std::cout << "SURGE durable ClassificationProposal: atomic commit, lineage, restart retrieval passed\n";
}
