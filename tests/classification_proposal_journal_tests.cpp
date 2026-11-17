#include "test_support.hpp"
#include "surge/c_api.h"
#include "surge/sha256.hpp"
#include <filesystem>
#include <iostream>
#include <string>
using surge::sha256_hex;
int main() {
  const std::string source = "text";
  const std::string proposal_json =
      R"({"proposal_id":"proposal-1","packet_ref":"packet-1","schema_version":"0.9.0"})";
  const auto root = std::filesystem::temp_directory_path() / "surge-classification-proposal-journal-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto path = (root / "proposal.sgj").string();
  SurgeRecognitionPacketInput packet{};
  packet.packet_id="packet-1"; packet.idempotency_key="packet-idem-1";
  packet.context_id="context-1"; packet.source_id="source-1";
  packet.source_generation="generation-1"; packet.block_ref="block-1";
  packet.offset_basis="UTF8_BYTE_NFC"; packet.start_offset=0;
  packet.end_offset=source.size(); packet.source_sequence=1;
  const auto digest=sha256_hex(source);
  packet.source_snapshot_digest=digest.c_str(); packet.source_range_digest=digest.c_str();
  const auto request_digest=sha256_hex("canonical-packet");
  packet.canonical_request_digest=request_digest.c_str(); packet.revision=0;
  const std::string node_id="evidence-1", node_kind="EVIDENCE", node_content="text evidence";
  SurgeGraphNodeInput node{node_id.c_str(),node_kind.c_str(),node_content.c_str(),nullptr,0};
  const std::string proposal_id="proposal-1", packet_ref="packet-1", generation="generation-1";
  SurgeClassificationProposalInput proposal{proposal_id.c_str(),packet_ref.c_str(),0,0,
      generation.c_str(),1,proposal_json.c_str()};
  const std::string observed_at="2026-11-17T09:00:00+09:00";
  const std::string retrieved_at="2026-11-17T09:05:00+09:00";
  SurgeEvidenceProvenanceInput provenance{node_id.c_str(),observed_at.c_str(),
      nullptr,retrieved_at.c_str(),1};
  {
    char error[512]{};
    void* handle=surge_packet_core_open(path.c_str(),error,sizeof(error)); CHECK(handle);
    int duplicate=0; std::uint64_t version=0; char status[128]{};
    const int committed=surge_packet_core_admit_and_apply_with_metadata(
      handle,&packet,reinterpret_cast<const unsigned char*>(source.data()),source.size(),
      "delta-1","semantic-idem-1",0,&node,1,nullptr,0,nullptr,0,nullptr,nullptr,
      &proposal,&provenance,&duplicate,&version,status,sizeof(status),error,sizeof(error));
    CHECK(committed==1); CHECK(version==1); CHECK(duplicate==0);
    surge_packet_core_close(handle);
  }
  {
    char error[512]{};
    void* handle=surge_packet_core_open(path.c_str(),error,sizeof(error)); CHECK(handle);
    std::size_t required=0;
    const int size_query=surge_classification_proposal_get(handle,proposal_id.c_str(),
      nullptr,0,&required,error,sizeof(error));
    CHECK(size_query==0); CHECK(std::string(error)=="CLASSIFICATION_PROPOSAL_BUFFER_TOO_SMALL");
    CHECK(required==proposal_json.size()+1);
    std::string output(required,'\0');
    const int loaded=surge_classification_proposal_get(handle,proposal_id.c_str(),
      output.data(),output.size(),&required,error,sizeof(error));
    CHECK(loaded==1); CHECK(std::string(output.c_str())==proposal_json);
    required=0;
    const int provenance_size=surge_evidence_provenance_get(handle,node_id.c_str(),
      nullptr,0,&required,error,sizeof(error));
    CHECK(provenance_size==0);
    CHECK(std::string(error)=="EVIDENCE_PROVENANCE_BUFFER_TOO_SMALL");
    std::string provenance_json(required,'\0');
    const int provenance_loaded=surge_evidence_provenance_get(handle,node_id.c_str(),
      provenance_json.data(),provenance_json.size(),&required,error,sizeof(error));
    CHECK(provenance_loaded==1);
    CHECK(provenance_json.find(observed_at)!=std::string::npos);
    CHECK(provenance_json.find("\"source_published_at\":null")!=std::string::npos);
    CHECK(provenance_json.find("\"recorded_state_version\":1")!=std::string::npos);
    surge_packet_core_close(handle);
  }
  {
    const auto invalid_path=(root/"invalid-provenance.sgj").string();
    char error[512]{}; void* handle=surge_packet_core_open(invalid_path.c_str(),error,sizeof(error)); CHECK(handle);
    const std::string invalid_time="2026-02-30T12:00:00Z";
    SurgeEvidenceProvenanceInput invalid{node_id.c_str(),invalid_time.c_str(),nullptr,nullptr,1};
    int duplicate=0; std::uint64_t version=99; char status[128]{};
    const int committed=surge_packet_core_admit_and_apply_with_metadata(
      handle,&packet,reinterpret_cast<const unsigned char*>(source.data()),source.size(),
      "delta-invalid","semantic-idem-invalid",0,&node,1,nullptr,0,nullptr,0,nullptr,nullptr,
      nullptr,&invalid,&duplicate,&version,status,sizeof(status),error,sizeof(error));
    CHECK(committed==0);
    CHECK(std::string(error)=="EVIDENCE_PROVENANCE_TIMESTAMP_INVALID");
    CHECK(surge_packet_core_state_version(handle)==0);
    surge_packet_core_close(handle);
  }
  {
    const auto reject_path=(root/"reject.sgj").string();
    char error[512]{}; void* handle=surge_packet_core_open(reject_path.c_str(),error,sizeof(error)); CHECK(handle);
    SurgeClassificationProposalInput stale=proposal; stale.base_state_version=7;
    int duplicate=0; std::uint64_t version=99; char status[128]{};
    const int committed=surge_packet_core_admit_and_apply_with_proposal(
      handle,&packet,reinterpret_cast<const unsigned char*>(source.data()),source.size(),
      "delta-1","semantic-idem-1",0,&node,1,nullptr,0,nullptr,0,nullptr,nullptr,
      &stale,&duplicate,&version,status,sizeof(status),error,sizeof(error));
    CHECK(committed==0); CHECK(std::string(error)=="CLASSIFICATION_PROPOSAL_LINEAGE_INVALID");
    CHECK(surge_packet_core_state_version(handle)==0); surge_packet_core_close(handle);
  }
  {
    const auto legacy_path=(root/"legacy.sgj").string();
    char error[512]{}; void* handle=surge_packet_core_open(legacy_path.c_str(),error,sizeof(error)); CHECK(handle);
    auto legacy=packet; legacy.packet_id="packet-legacy"; legacy.idempotency_key="packet-idem-legacy";
    const auto legacy_digest=sha256_hex("legacy-canonical"); legacy.canonical_request_digest=legacy_digest.c_str();
    const std::string legacy_node_id="evidence-legacy", legacy_node_content="legacy evidence";
    SurgeGraphNodeInput legacy_node{legacy_node_id.c_str(),node_kind.c_str(),legacy_node_content.c_str(),nullptr,0};
    int duplicate=0; std::uint64_t version=0; char status[128]{};
    const int committed=surge_packet_core_admit_and_apply(handle,&legacy,
      reinterpret_cast<const unsigned char*>(source.data()),source.size(),
      "delta-legacy","semantic-idem-legacy",0,&legacy_node,1,nullptr,0,nullptr,0,
      nullptr,nullptr,&duplicate,&version,status,sizeof(status),error,sizeof(error));
    CHECK(committed==1); CHECK(version==1); surge_packet_core_close(handle);
  }
  std::filesystem::remove_all(root);
  std::cout<<"SURGE durable ClassificationProposal tests passed\n";
}
