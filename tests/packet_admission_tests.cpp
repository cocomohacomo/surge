#include "test_support.hpp"
#include "surge/packet_admission.hpp"
#include "surge/context_graph.hpp"
#include "surge/sha256.hpp"
#include "surge/journal_replay_audit.hpp"
#include <filesystem>
#include <iostream>
using namespace surge;
static ValidatedRecognitionPacket packet(const std::string& id,const std::string& idem,
                                         std::uint64_t seq,const std::string& source) {
  ValidatedRecognitionPacket p;
  p.packet_id=id; p.idempotency_key=idem; p.context_id="ctx";
  p.source_id="doc"; p.source_generation="g1"; p.block_ref="b1";
  p.offset_basis="UTF8_BYTE_NFC"; p.start_offset=0; p.end_offset=3;
  p.source_sequence=seq; p.source_snapshot_digest=sha256_hex(source);
  p.source_range_digest=sha256_hex(source);
  p.canonical_request_digest="canonical-"+id;
  return p;
}
int main() {
  CHECK(sha256_hex("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256_hex("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(is_valid_utf8("日本語"));
  CHECK(!is_valid_utf8(std::string(1, static_cast<char>(0xff))));
  const auto root=std::filesystem::temp_directory_path()/"surge-packet-admission";
  std::filesystem::remove_all(root);
  const auto path=(root/"events.sgj").string();
  {
    EventJournal journal(path); CHECK(journal.open());
    PacketAdmissionCoordinator coordinator(journal); CHECK(coordinator.recover());
    auto p1=packet("p1","i1",1,"abc");
    auto first=coordinator.admit(p1,"abc");
    CHECK(first.admitted&&!first.duplicate&&first.state_version==1&&first.watermark==1);
    CHECK(first.packet_status=="COMMITTED");
    auto retry=coordinator.admit(p1,"abc");
    CHECK(retry.admitted&&retry.duplicate&&coordinator.packet_count()==1);
    CHECK(journal.state_version()==1);
    auto p2=packet("p2","i2",3,"abc"); p2.block_ref="b2";
    auto gap=coordinator.admit(p2,"abc");
    CHECK(gap.admitted&&gap.watermark==1&&gap.packet_status=="READY_BUT_BLOCKED");
    auto p3=packet("p3","i3",4,"abc"); p3.block_ref="b3";
    CHECK(coordinator.admit(p3,"abc").admitted);
    auto changed=packet("p4","i4",5,"abd"); changed.block_ref="b3";
    CHECK(coordinator.admit(changed,"abd").error=="RANGE_CONTENT_CONFLICT");
    auto tampered=packet("p5","i5",6,"abc"); tampered.block_ref="b5";
    tampered.source_range_digest=std::string(64,'e');
    const auto bad_range=coordinator.admit(tampered,"abc");
    CHECK(bad_range.error=="SOURCE_RANGE_DIGEST_MISMATCH");
    CHECK(bad_range.failure_stage=="SOURCE_VALIDATION");
    CHECK(bad_range.failure_code=="SOURCE_RANGE_DIGEST_MISMATCH");
    auto wrong_snapshot=packet("p5b","i5b",6,"abc"); wrong_snapshot.block_ref="b5b";
    CHECK(coordinator.admit(wrong_snapshot,"abd").error=="SOURCE_SNAPSHOT_DIGEST_MISMATCH");
    auto bad_utf8=packet("p5c","i5c",6,"abc"); bad_utf8.block_ref="b5c";
    CHECK(coordinator.admit(bad_utf8,std::string(1, static_cast<char>(0xff))).error=="SOURCE_SNAPSHOT_INVALID_UTF8");
    auto collision=packet("p1","i-new",7,"abc"); collision.block_ref="b6";
    collision.canonical_request_digest="changed-canonical-request";
    const auto reused=coordinator.admit(collision,"abc");
    CHECK(reused.error=="PACKET_ID_REUSED");
    CHECK(reused.failure_stage=="IDENTITY_VALIDATION");
    CHECK(reused.failure_code=="PACKET_ID_REUSED");
    // A caller-controlled digest must not make a different envelope look identical.
    auto forged_digest=packet("p1","i-forged",7,"abc"); forged_digest.block_ref="b7";
    forged_digest.canonical_request_digest="canonical-p1";
    CHECK(coordinator.admit(forged_digest,"abc").error=="PACKET_ID_REUSED");
    CHECK(coordinator.packet_count()==3);
  }
  {
    EventJournal journal(path); CHECK(journal.open());
    PacketAdmissionCoordinator recovered(journal); CHECK(recovered.recover());
    CHECK(recovered.packet_count()==3&&recovered.state_version()==3);
    auto retry=recovered.admit(packet("p1","i1",1,"abc"),"abc");
    CHECK(retry.admitted&&retry.duplicate);
    auto p6=packet("p6","i6",2,"abc"); p6.block_ref="b4";
    auto next=recovered.admit(p6,"abc");
    CHECK(next.admitted&&next.watermark==4&&journal.state_version()==4);
  }
  {
    EventJournal journal((root/"revision.sgj").string()); CHECK(journal.open());
    PacketAdmissionCoordinator coordinator(journal); CHECK(coordinator.recover());
    auto original=packet("original","rev-i1",1,"abc"); original.block_ref="revision-block";
    CHECK(coordinator.admit(original,"abc").admitted);
    auto wrong=packet("wrong-parent","rev-i2",2,"abd"); wrong.block_ref="revision-block";
    wrong.revision=true; wrong.revision_sequence=1; wrong.revision_of_packet_id="not-original";
    const auto wrong_revision=coordinator.admit(wrong,"abd");
    CHECK(wrong_revision.error=="REVISION_TARGET_MISMATCH");
    CHECK(wrong_revision.failure_stage=="PACKET_SEQUENCE_VALIDATION");
    CHECK(wrong_revision.failure_code=="REVISION_TARGET_MISMATCH");
    auto revision=packet("revision-1","rev-i3",2,"abd"); revision.block_ref="revision-block";
    revision.revision=true; revision.revision_sequence=1; revision.revision_of_packet_id="original";
    CHECK(coordinator.admit(revision,"abd").admitted);
    auto far=packet("too-far","rev-i4",5000,"abc"); far.block_ref="far-block";
    CHECK(coordinator.admit(far,"abc").error=="SOURCE_SEQUENCE_GAP_EXCEEDS_LIMIT");
    CHECK(coordinator.packet_count()==2);
  }
  {
    const auto atomic_path=(root/"atomic-semantic-packet.sgj").string();
    {
      EventJournal journal(atomic_path);CHECK(journal.open());
      ContextGraph graph(journal);CHECK(graph.open());
      PacketAdmissionCoordinator coordinator(journal);CHECK(coordinator.recover());
      SemanticDelta delta;delta.delta_id="atomic-delta";delta.idempotency_key="atomic-semantic-key";
      delta.expected_state_version=0;
      delta.nodes={{{"atomic-source",NodeKind::Cluster,"approved",{}}},
                   {{"atomic-target",NodeKind::Cluster,"target",{}}},
                   {{"atomic-dependency",NodeKind::Dependency,"CONTENT_EQUALS:approved",
                     {"atomic-source","atomic-target"}}}};
      std::vector<JournalEvent> semantic_events;std::string error;
      CHECK(graph.prepare_events(delta,semantic_events,error));
      CHECK(semantic_events.size()==5);
      const auto admitted=coordinator.admit(packet("atomic-packet","atomic-packet-key",1,"abc"),
          "abc",semantic_events);
      CHECK(admitted.admitted&&!admitted.duplicate&&admitted.state_version==1);
      CHECK(graph.open());
      CHECK(graph.state_version()==1);
      CHECK(graph.dependency_evaluations().count("atomic-delta")==1);
      CHECK(graph.dependency_evaluations().at("atomic-delta").triggered.size()==1);
    }
    {
      EventJournal journal(atomic_path);CHECK(journal.open());
      ContextGraph recovered(journal);CHECK(recovered.open());
      PacketAdmissionCoordinator coordinator(journal);CHECK(coordinator.recover());
      CHECK(journal.state_version()==1&&coordinator.packet_count()==1);
      CHECK(recovered.dependency_evaluations().count("atomic-delta")==1);
      CHECK(recovered.dependency_evaluations().at("atomic-delta").triggered[0].dependency_id==
          "atomic-dependency");
    }
  }
  {
    const auto document_path=(root/"document-stream.sgj").string();
    {
      EventJournal journal(document_path); CHECK(journal.open());
      PacketAdmissionCoordinator coordinator(journal); CHECK(coordinator.recover());
      auto make_document_packet = [&](const std::string& id, std::uint64_t source_seq,
                                      std::uint64_t doc_seq, std::uint64_t start,
                                      std::uint64_t end) {
        auto p=packet(id,"document-idem-"+id,source_seq,"abc");
        p.block_ref="document-block-"+std::to_string(doc_seq);
        p.document_ref="document-42";
        p.document_generation="revision-3";
        p.document_sequence=doc_seq;
        p.global_start_offset=start;
        p.global_end_offset=end;
        return p;
      };
      auto out_of_order=make_document_packet("doc-2",1,2,3,6);
      CHECK(coordinator.admit(out_of_order,"abc").error=="DOCUMENT_SEQUENCE_GAP");
      auto malformed_range=make_document_packet("doc-malformed",1,1,0,4);
      CHECK(coordinator.admit(malformed_range,"abc").error=="PACKET_CORE_INVARIANT_FAILED");
      auto first=make_document_packet("doc-1",1,1,0,3);
      CHECK(coordinator.admit(first,"abc").admitted);
      auto gap=make_document_packet("doc-3",2,2,4,7);
      CHECK(coordinator.admit(gap,"abc").error=="DOCUMENT_GLOBAL_RANGE_GAP");
      auto overlap=make_document_packet("doc-4",2,2,2,5);
      CHECK(coordinator.admit(overlap,"abc").error=="DOCUMENT_GLOBAL_RANGE_OVERLAP");
      auto second=make_document_packet("doc-2",2,2,3,6);
      CHECK(coordinator.admit(second,"abc").admitted);
      auto third=make_document_packet("doc-3",3,3,6,9);
      CHECK(coordinator.admit(third,"abc").admitted);
    }
    {
      EventJournal journal(document_path); CHECK(journal.open());
      PacketAdmissionCoordinator recovered(journal); CHECK(recovered.recover());
      CHECK(recovered.packet_count()==3);
      auto next=packet("doc-4","document-idem-doc-4",4,"abc");
      next.block_ref="document-block-4";
      next.document_ref="document-42";
      next.document_generation="revision-3";
      next.document_sequence=4;
      next.global_start_offset=9;
      next.global_end_offset=12;
      CHECK(recovered.admit(next,"abc").admitted);
    }
  }

  {
    const auto fault_path=(root/"append-fault.sgj").string();
    EventJournal failing(fault_path, [](std::size_t frame_index) {
      return frame_index != 0;  // fail before BEGIN frame reaches the Journal
    });
    CHECK(failing.open());
    PacketAdmissionCoordinator coordinator(failing); CHECK(coordinator.recover());
    const auto rejected=coordinator.admit(packet("fault-p1","fault-i1",1,"abc"),"abc");
    CHECK(!rejected.admitted);
    CHECK(rejected.commit_state==AdmissionCommitState::NotCommitted);
    CHECK(rejected.failure_stage=="JOURNAL_COMMIT");
    CHECK(rejected.failure_code=="JOURNAL_INJECTED_APPEND_FAILURE");
    CHECK(failing.state_version()==0);
  }
  {
    const auto restart_path=(root/"commit-before-projection-restart.sgj").string();
    std::string committed_digest;
    {
      EventJournal journal(restart_path); CHECK(journal.open());
      ContextGraph graph(journal); CHECK(graph.open());
      PacketAdmissionCoordinator coordinator(journal); CHECK(coordinator.recover());
      SemanticDelta delta; delta.delta_id="restart-delta";
      delta.idempotency_key="restart-delta-idem"; delta.expected_state_version=0;
      delta.nodes={{{"restart-cluster",NodeKind::Cluster,"recovered",{}}}};
      std::vector<JournalEvent> semantic; std::string error;
      CHECK(graph.prepare_events(delta,semantic,error));
      const auto admitted=coordinator.admit(
          packet("restart-packet","restart-packet-idem",1,"abc"),"abc",semantic);
      CHECK(admitted.admitted && admitted.commit_state==AdmissionCommitState::Committed);
      // Deliberately leave the live Graph projection unapplied before restart.
      CHECK(graph.nodes().empty());
      CHECK(journal.state_version()==1);
    }
    {
      EventJournal journal(restart_path); CHECK(journal.open());
      {
        ContextGraph failed_replay(journal);
        failed_replay.set_replay_gate_for_testing([] { return false; });
        CHECK(!failed_replay.open());
      }
      // A fresh process-equivalent Graph instance must recover the durable
      // transaction after the injected transient projection replay failure.
      ContextGraph graph(journal); CHECK(graph.open());
      PacketAdmissionCoordinator coordinator(journal); CHECK(coordinator.recover());
      CHECK(coordinator.packet_count()==1);
      CHECK(graph.nodes().count("restart-cluster")==1);
      const auto duplicate=coordinator.admit(
          packet("restart-packet","restart-packet-idem",1,"abc"),"abc");
      CHECK(duplicate.admitted && duplicate.duplicate);
      CHECK(journal.state_version()==1 && coordinator.packet_count()==1);
      const auto digest=graph.full_state_digest();
      std::string expected,error;
      CHECK(graph.journal_expected_full_state_digest(expected,error));
      CHECK(digest==expected);
      const auto audit=audit_journal_replay(
          journal,coordinator,graph,graph.node_relation_digest());
      CHECK(audit.ok);
      CHECK(audit.journal_packet_count==1);
      CHECK(audit.coordinator_packet_count==1);
      CHECK(audit.semantic_packet_count==1);
    }
  }
  std::filesystem::remove_all(root);
  std::cout<<"SURGE PacketAdmission + SHA-256 tests passed\\n";
}
