#include "surge/packet_admission.hpp"
#include "surge/sha256.hpp"
#include <cassert>
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
  assert(sha256_hex("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  assert(sha256_hex("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  assert(is_valid_utf8("日本語"));
  assert(!is_valid_utf8(std::string(1, static_cast<char>(0xff))));
  const auto root=std::filesystem::temp_directory_path()/"surge-packet-admission";
  std::filesystem::remove_all(root);
  const auto path=(root/"events.sgj").string();
  {
    EventJournal journal(path); assert(journal.open());
    PacketAdmissionCoordinator coordinator(journal); assert(coordinator.recover());
    auto p1=packet("p1","i1",1,"abc");
    auto first=coordinator.admit(p1,"abc");
    assert(first.admitted&&!first.duplicate&&first.state_version==1&&first.watermark==1);
    assert(first.packet_status=="COMMITTED");
    auto retry=coordinator.admit(p1,"abc");
    assert(retry.admitted&&retry.duplicate&&coordinator.packet_count()==1);
    assert(journal.state_version()==1);
    auto p2=packet("p2","i2",3,"abc"); p2.block_ref="b2";
    auto gap=coordinator.admit(p2,"abc");
    assert(gap.admitted&&gap.watermark==1&&gap.packet_status=="READY_BUT_BLOCKED");
    auto p3=packet("p3","i3",4,"abc"); p3.block_ref="b3";
    assert(coordinator.admit(p3,"abc").admitted);
    auto changed=packet("p4","i4",5,"abd"); changed.block_ref="b3";
    assert(coordinator.admit(changed,"abd").error=="RANGE_CONTENT_CONFLICT");
    auto tampered=packet("p5","i5",6,"abc"); tampered.block_ref="b5";
    tampered.source_range_digest=std::string(64,'e');
    assert(coordinator.admit(tampered,"abc").error=="SOURCE_RANGE_DIGEST_MISMATCH");
    auto wrong_snapshot=packet("p5b","i5b",6,"abc"); wrong_snapshot.block_ref="b5b";
    assert(coordinator.admit(wrong_snapshot,"abd").error=="SOURCE_SNAPSHOT_DIGEST_MISMATCH");
    auto bad_utf8=packet("p5c","i5c",6,"abc"); bad_utf8.block_ref="b5c";
    assert(coordinator.admit(bad_utf8,std::string("\\xff",1)).error=="SOURCE_SNAPSHOT_INVALID_UTF8");
    auto collision=packet("p1","i-new",7,"abc"); collision.block_ref="b6";
    assert(coordinator.admit(collision,"abc").error=="PACKET_ID_REUSED");
    assert(coordinator.packet_count()==3);
  }
  {
    EventJournal journal(path); assert(journal.open());
    PacketAdmissionCoordinator recovered(journal); assert(recovered.recover());
    assert(recovered.packet_count()==3&&recovered.state_version()==3);
    auto retry=recovered.admit(packet("p1","i1",1,"abc"),"abc");
    assert(retry.admitted&&retry.duplicate);
    auto p6=packet("p6","i6",2,"abc"); p6.block_ref="b4";
    auto next=recovered.admit(p6,"abc");
    assert(next.admitted&&next.watermark==4&&journal.state_version()==4);
  }
  {
    EventJournal journal((root/"revision.sgj").string()); assert(journal.open());
    PacketAdmissionCoordinator coordinator(journal); assert(coordinator.recover());
    auto original=packet("original","rev-i1",1,"abc"); original.block_ref="revision-block";
    assert(coordinator.admit(original,"abc").admitted);
    auto wrong=packet("wrong-parent","rev-i2",2,"abd"); wrong.block_ref="revision-block";
    wrong.revision=true; wrong.revision_sequence=1; wrong.revision_of_packet_id="not-original";
    assert(coordinator.admit(wrong,"abd").error=="REVISION_TARGET_MISMATCH");
    auto revision=packet("revision-1","rev-i3",2,"abd"); revision.block_ref="revision-block";
    revision.revision=true; revision.revision_sequence=1; revision.revision_of_packet_id="original";
    assert(coordinator.admit(revision,"abd").admitted);
    auto far=packet("too-far","rev-i4",5000,"abc"); far.block_ref="far-block";
    assert(coordinator.admit(far,"abc").error=="SOURCE_SEQUENCE_GAP_EXCEEDS_LIMIT");
    assert(coordinator.packet_count()==2);
  }
  std::filesystem::remove_all(root);
  std::cout<<"SURGE PacketAdmission + SHA-256 tests passed\\n";
}
