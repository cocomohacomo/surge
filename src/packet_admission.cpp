#include "surge/packet_admission.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace surge {
namespace {
std::string key_part(const std::string& value) {
  return std::to_string(value.size()) + ":" + value;
}
std::string packet_event_id(const std::string& packet_id) {
  return "recognition-packet:" + packet_id;
}
}  // namespace

PacketAdmissionCoordinator::PacketAdmissionCoordinator(EventJournal& journal)
    : journal_(journal) {}

std::string PacketAdmissionCoordinator::stream_key(
    const ValidatedRecognitionPacket& p) {
  return key_part(p.context_id) + key_part(p.source_id) +
         key_part(p.source_generation);
}

std::string PacketAdmissionCoordinator::range_key(
    const ValidatedRecognitionPacket& p) {
  return stream_key(p) + key_part(p.block_ref) + key_part(p.offset_basis) +
         std::to_string(p.start_offset) + ":" + std::to_string(p.end_offset);
}

std::string PacketAdmissionCoordinator::encode(
    const ValidatedRecognitionPacket& p) {
  std::ostringstream out;
  out << "RPA1 " << std::quoted(p.packet_id) << ' '
      << std::quoted(p.idempotency_key) << ' ' << std::quoted(p.context_id) << ' '
      << std::quoted(p.source_id) << ' ' << std::quoted(p.source_generation) << ' '
      << std::quoted(p.block_ref) << ' ' << std::quoted(p.offset_basis) << ' '
      << p.start_offset << ' ' << p.end_offset << ' ' << p.source_sequence << ' '
      << std::quoted(p.source_range_digest) << ' '
      << std::quoted(p.canonical_request_digest) << ' '
      << (p.revision ? 1 : 0) << ' ' << p.revision_sequence;
  return out.str();
}

bool PacketAdmissionCoordinator::decode(
    const std::string& payload, ValidatedRecognitionPacket& p) {
  std::istringstream in(payload);
  std::string magic;
  int revision = 0;
  if (!(in >> magic) || magic != "RPA1" ||
      !(in >> std::quoted(p.packet_id) >> std::quoted(p.idempotency_key)
        >> std::quoted(p.context_id) >> std::quoted(p.source_id)
        >> std::quoted(p.source_generation) >> std::quoted(p.block_ref)
        >> std::quoted(p.offset_basis) >> p.start_offset >> p.end_offset
        >> p.source_sequence >> std::quoted(p.source_range_digest)
        >> std::quoted(p.canonical_request_digest) >> revision
        >> p.revision_sequence)) return false;
  std::string extra;
  if (in >> extra) return false;
  if (revision != 0 && revision != 1) return false;
  p.revision = revision == 1;
  // These flags record the adapter's validation at original admission. The
  // journal is trusted on replay, so no caller-supplied flag is serialized.
  p.source_snapshot_verified = true;
  p.range_digest_verified = true;
  return true;
}

bool PacketAdmissionCoordinator::apply_recovered(
    const ValidatedRecognitionPacket& p) {
  const auto skey = stream_key(p);
  auto& state = streams_[skey];
  const auto rkey = range_key(p);
  const auto range = state.ranges.ingest(rkey, p.source_range_digest,
      p.idempotency_key, p.revision, p.revision_sequence);
  if (!range.error.empty()) return false;
  auto seq = state.sequence_packets.find(p.source_sequence);
  if (seq != state.sequence_packets.end() && seq->second != p.packet_id)
    return false;
  state.sequence_packets[p.source_sequence] = p.packet_id;
  state.sequencer.observe(p.source_sequence);
  packet_digests_[p.packet_id] = p.canonical_request_digest;
  idempotency_packets_[p.idempotency_key] = p.packet_id;
  return true;
}

bool PacketAdmissionCoordinator::recover() {
  streams_.clear();
  packet_digests_.clear();
  idempotency_packets_.clear();
  if (journal_.state_version() == 0 && journal_.committed_events().empty()) {
    recovered_ = true;
    return true;
  }
  for (const auto& event : journal_.committed_events()) {
    if (event.kind != "RECOGNITION_PACKET_ADMITTED") continue;
    ValidatedRecognitionPacket packet;
    if (!decode(event.payload, packet) || !apply_recovered(packet)) {
      recovered_ = false;
      return false;
    }
  }
  recovered_ = true;
  return true;
}

PacketAdmissionResult PacketAdmissionCoordinator::admit(
    const ValidatedRecognitionPacket& p) {
  PacketAdmissionResult result;
  result.state_version = journal_.state_version();
  if (!recovered_) { result.error = "PACKET_COORDINATOR_NOT_RECOVERED"; return result; }
  if (!p.source_snapshot_verified || !p.range_digest_verified) {
    result.error = "SOURCE_VALIDATION_REQUIRED"; return result;
  }
  if (p.packet_id.empty() || p.idempotency_key.empty() || p.context_id.empty() ||
      p.source_id.empty() || p.source_generation.empty() || p.block_ref.empty() ||
      p.source_range_digest.size() != 64 || p.canonical_request_digest.empty() ||
      p.offset_basis != "UTF8_BYTE_NFC" || p.end_offset <= p.start_offset ||
      p.source_sequence == 0 || (p.revision && p.revision_sequence == 0) ||
      (!p.revision && p.revision_sequence != 0)) {
    result.error = "PACKET_CORE_INVARIANT_FAILED"; return result;
  }

  auto existing_packet = packet_digests_.find(p.packet_id);
  if (existing_packet != packet_digests_.end()) {
    if (existing_packet->second != p.canonical_request_digest) {
      result.error = "PACKET_ID_REUSED"; return result;
    }
    result.admitted = true;
    result.duplicate = true;
    result.packet_status = "DUPLICATE";
    result.state_version = journal_.state_version();
    const auto& stream = streams_.at(stream_key(p));
    result.watermark = stream.sequencer.watermark();
    return result;
  }
  auto existing_idem = idempotency_packets_.find(p.idempotency_key);
  if (existing_idem != idempotency_packets_.end()) {
    result.error = existing_idem->second == p.packet_id
        ? "PACKET_ID_REUSED" : "IDEMPOTENCY_KEY_REUSED";
    return result;
  }

  const auto skey = stream_key(p);
  StreamState staged;
  auto old_stream = streams_.find(skey);
  if (old_stream != streams_.end()) staged = old_stream->second;

  const auto sequence = staged.sequence_packets.find(p.source_sequence);
  if (sequence != staged.sequence_packets.end() && sequence->second != p.packet_id) {
    result.error = "SOURCE_SEQUENCE_REUSED"; return result;
  }
  const auto range = staged.ranges.ingest(range_key(p), p.source_range_digest,
      p.idempotency_key, p.revision, p.revision_sequence);
  if (!range.error.empty()) { result.error = range.error; return result; }

  staged.sequence_packets[p.source_sequence] = p.packet_id;
  staged.sequencer.observe(p.source_sequence);

  const auto expected = journal_.state_version();
  const std::string tx = "recognition-packet-tx:" + p.packet_id;
  const auto committed = journal_.commit(tx, p.idempotency_key, expected,
      {{packet_event_id(p.packet_id), "RECOGNITION_PACKET_ADMITTED", encode(p)}});
  if (!committed.committed) {
    result.error = committed.error;
    result.state_version = committed.state_version;
    return result;
  }
  if (committed.duplicate) {
    // A duplicate journal commit must already be reflected by recovered state.
    result.error = "JOURNAL_DUPLICATE_WITHOUT_COORDINATOR_STATE";
    return result;
  }

  streams_[skey] = std::move(staged);
  packet_digests_[p.packet_id] = p.canonical_request_digest;
  idempotency_packets_[p.idempotency_key] = p.packet_id;
  result.admitted = true;
  result.state_version = committed.state_version;
  result.watermark = streams_.at(skey).sequencer.watermark();
  result.packet_status = streams_.at(skey).sequencer.packet_status(p.source_sequence);
  return result;
}

std::uint64_t PacketAdmissionCoordinator::state_version() const {
  return journal_.state_version();
}
std::size_t PacketAdmissionCoordinator::packet_count() const {
  return packet_digests_.size();
}
}  // namespace surge
