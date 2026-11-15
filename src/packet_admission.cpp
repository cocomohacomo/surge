#include "surge/packet_admission.hpp"
#include "surge/sha256.hpp"

#include <iomanip>
#include <limits>
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
  out << "RPA2 " << std::quoted(p.packet_id) << ' '
      << std::quoted(p.idempotency_key) << ' ' << std::quoted(p.context_id) << ' '
      << std::quoted(p.source_id) << ' ' << std::quoted(p.source_generation) << ' '
      << std::quoted(p.block_ref) << ' ' << std::quoted(p.offset_basis) << ' '
      << p.start_offset << ' ' << p.end_offset << ' ' << p.source_sequence << ' '
      << std::quoted(p.source_snapshot_digest) << ' '
      << std::quoted(p.source_range_digest) << ' '
      << std::quoted(p.canonical_request_digest) << ' '
      << std::quoted(p.revision_of_packet_id) << ' '
      << (p.revision ? 1 : 0) << ' ' << p.revision_sequence;
  return out.str();
}

bool PacketAdmissionCoordinator::decode(
    const std::string& payload, ValidatedRecognitionPacket& p) {
  std::istringstream in(payload);
  std::string magic;
  int revision = 0;
  if (!(in >> magic) || (magic != "RPA1" && magic != "RPA2")) return false;
  if (!(in >> std::quoted(p.packet_id) >> std::quoted(p.idempotency_key)
        >> std::quoted(p.context_id) >> std::quoted(p.source_id)
        >> std::quoted(p.source_generation) >> std::quoted(p.block_ref)
        >> std::quoted(p.offset_basis) >> p.start_offset >> p.end_offset
        >> p.source_sequence)) return false;
  if (magic == "RPA2" && !(in >> std::quoted(p.source_snapshot_digest))) return false;
  if (!(in >> std::quoted(p.source_range_digest)
        >> std::quoted(p.canonical_request_digest)
        >> std::quoted(p.revision_of_packet_id) >> revision
        >> p.revision_sequence)) return false;
  std::string extra;
  if (in >> extra) return false;
  if (revision != 0 && revision != 1) return false;
  p.revision = revision == 1;
  return true;
}

bool PacketAdmissionCoordinator::apply_recovered(
    const ValidatedRecognitionPacket& p) {
  const auto skey = stream_key(p);
  auto& state = streams_[skey];
  const auto rkey = range_key(p);
  const auto latest = state.range_latest_packets.find(rkey);
  if (p.revision) {
    if (latest == state.range_latest_packets.end() ||
        p.revision_of_packet_id != latest->second) return false;
  } else if (!p.revision_of_packet_id.empty()) {
    return false;
  }
  const auto range = state.ranges.ingest(rkey, p.source_range_digest,
      p.idempotency_key, p.revision, p.revision_sequence);
  if (!range.error.empty()) return false;
  state.range_latest_packets[rkey] = p.packet_id;
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
    const ValidatedRecognitionPacket& p,
    const std::string& normalized_source_snapshot) {
  PacketAdmissionResult result;
  result.state_version = journal_.state_version();
  if (!recovered_) { result.error = "PACKET_COORDINATOR_NOT_RECOVERED"; return result; }
   if (p.packet_id.empty() || p.idempotency_key.empty() || p.context_id.empty() ||
      p.source_id.empty() || p.source_generation.empty() || p.block_ref.empty() ||
      p.source_snapshot_digest.size() != 64 || p.source_range_digest.size() != 64 || p.canonical_request_digest.empty() ||
      p.offset_basis != "UTF8_BYTE_NFC" || p.end_offset <= p.start_offset ||
      p.source_sequence == 0 || p.source_sequence == std::numeric_limits<std::uint64_t>::max() ||
      (p.revision && (p.revision_sequence == 0 || p.revision_of_packet_id.empty())) ||
      (!p.revision && (p.revision_sequence != 0 || !p.revision_of_packet_id.empty()))) {
    result.error = "PACKET_CORE_INVARIANT_FAILED"; return result;
  }
  if (!is_valid_utf8(normalized_source_snapshot)) {
    result.error = "SOURCE_SNAPSHOT_INVALID_UTF8"; return result;
  }
  if (p.end_offset > normalized_source_snapshot.size() ||
      sha256_hex(normalized_source_snapshot) != p.source_snapshot_digest) {
    result.error = "SOURCE_SNAPSHOT_DIGEST_MISMATCH"; return result;
  }
  const auto range_bytes = normalized_source_snapshot.substr(
      static_cast<std::size_t>(p.start_offset),
      static_cast<std::size_t>(p.end_offset - p.start_offset));
  if (sha256_hex(range_bytes) != p.source_range_digest) {
    result.error = "SOURCE_RANGE_DIGEST_MISMATCH"; return result;
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

  if (p.source_sequence > staged.sequencer.watermark() &&
      p.source_sequence - staged.sequencer.watermark() > 4096) {
    result.error = "SOURCE_SEQUENCE_GAP_EXCEEDS_LIMIT"; return result;
  }
  const auto sequence = staged.sequence_packets.find(p.source_sequence);
  if (sequence != staged.sequence_packets.end() && sequence->second != p.packet_id) {
    result.error = "SOURCE_SEQUENCE_REUSED"; return result;
  }
  const auto rkey = range_key(p);
  const auto latest = staged.range_latest_packets.find(rkey);
  if (p.revision) {
    if (latest == staged.range_latest_packets.end() ||
        p.revision_of_packet_id != latest->second) {
      result.error = "REVISION_TARGET_MISMATCH"; return result;
    }
  } else if (!p.revision_of_packet_id.empty()) {
    result.error = "REVISION_FIELDS_REQUIRE_REVISION_PACKET"; return result;
  }
  const auto range = staged.ranges.ingest(rkey, p.source_range_digest,
      p.idempotency_key, p.revision, p.revision_sequence);
  if (!range.error.empty()) { result.error = range.error; return result; }
  staged.range_latest_packets[rkey] = p.packet_id;

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
