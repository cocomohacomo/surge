#include "surge/packet_admission.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>

using namespace surge;

static ValidatedRecognitionPacket packet(const std::string& id,
                                         const std::string& idem,
                                         std::uint64_t seq,
                                         const std::string& digest) {
  ValidatedRecognitionPacket p;
  p.packet_id = id; p.idempotency_key = idem; p.context_id = "ctx";
  p.source_id = "doc"; p.source_generation = "g1"; p.block_ref = "b1";
  p.offset_basis = "UTF8_BYTE_NFC"; p.start_offset = 0; p.end_offset = 3;
  p.source_sequence = seq; p.source_range_digest = digest;
  p.canonical_request_digest = "canonical-" + id;
  p.source_snapshot_verified = true; p.range_digest_verified = true;
  return p;
}

int main() {
  const auto root = std::filesystem::temp_directory_path() / "surge-packet-admission";
  std::filesystem::remove_all(root);
  const auto path = (root / "events.sgj").string();
  {
    EventJournal journal(path);
    assert(journal.open());
    PacketAdmissionCoordinator coordinator(journal);
    assert(coordinator.recover());

    auto p1 = packet("p1", "i1", 1, std::string(64, 'a'));
    auto first = coordinator.admit(p1);
    assert(first.admitted && !first.duplicate);
    assert(first.state_version == 1 && first.watermark == 1);
    assert(first.packet_status == "COMMITTED");

    auto retry = coordinator.admit(p1);
    assert(retry.admitted && retry.duplicate);
    assert(coordinator.packet_count() == 1);
    assert(journal.state_version() == 1);

    auto p2 = packet("p2", "i2", 3, std::string(64, 'b'));
    p2.block_ref = "b2";
    auto gap = coordinator.admit(p2);
    assert(gap.admitted && gap.watermark == 1);
    assert(gap.packet_status == "READY_BUT_BLOCKED");

    auto conflict = packet("p3", "i3", 4, std::string(64, 'c'));
    conflict.block_ref = "b3";
    conflict.start_offset = 0; conflict.end_offset = 3;
    assert(coordinator.admit(conflict).admitted);
    auto same_range_changed = packet("p4", "i4", 5, std::string(64, 'd'));
    same_range_changed.block_ref = "b3";
    assert(coordinator.admit(same_range_changed).error == "RANGE_CONTENT_CONFLICT");

    auto untrusted = packet("p5", "i5", 6, std::string(64, 'e'));
    untrusted.block_ref = "b5";
    untrusted.range_digest_verified = false;
    assert(coordinator.admit(untrusted).error == "SOURCE_VALIDATION_REQUIRED");

    auto collision = packet("p1", "i-new", 7, std::string(64, 'f'));
    collision.block_ref = "b6";
    assert(coordinator.admit(collision).error == "PACKET_ID_REUSED");
    assert(coordinator.packet_count() == 3);
  }
  {
    EventJournal recovered_journal(path);
    assert(recovered_journal.open());
    PacketAdmissionCoordinator recovered(recovered_journal);
    assert(recovered.recover());
    assert(recovered.packet_count() == 3);
    assert(recovered.state_version() == 3);
    auto retry = recovered.admit(packet("p1", "i1", 1, std::string(64, 'a')));
    assert(retry.admitted && retry.duplicate);
    auto p6 = packet("p6", "i6", 2, std::string(64, 'f'));
    p6.block_ref = "b4";
    auto next = recovered.admit(p6);
    assert(next.admitted && next.watermark == 3);
    assert(recovered_journal.state_version() == 4);
  }

  {
    const auto revision_path = (root / "revision.sgj").string();
    EventJournal journal(revision_path);
    assert(journal.open());
    PacketAdmissionCoordinator coordinator(journal);
    assert(coordinator.recover());
    auto original = packet("original", "rev-i1", 1, std::string(64, 'a'));
    original.block_ref = "revision-block";
    assert(coordinator.admit(original).admitted);
    auto wrong_parent = packet("wrong-parent", "rev-i2", 2, std::string(64, 'b'));
    wrong_parent.block_ref = "revision-block";
    wrong_parent.revision = true;
    wrong_parent.revision_sequence = 1;
    wrong_parent.revision_of_packet_id = "not-original";
    assert(coordinator.admit(wrong_parent).error == "REVISION_TARGET_MISMATCH");
    auto revision = packet("revision-1", "rev-i3", 2, std::string(64, 'c'));
    revision.block_ref = "revision-block";
    revision.revision = true;
    revision.revision_sequence = 1;
    revision.revision_of_packet_id = "original";
    assert(coordinator.admit(revision).admitted);
    auto too_far = packet("too-far", "rev-i4", 5000, std::string(64, 'd'));
    too_far.block_ref = "far-block";
    assert(coordinator.admit(too_far).error == "SOURCE_SEQUENCE_GAP_EXCEEDS_LIMIT");
    assert(coordinator.packet_count() == 2);
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE PacketAdmission tests: 2 scenarios passed\n";
}
