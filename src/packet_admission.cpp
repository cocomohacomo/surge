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
std::string PacketAdmissionCoordinator::document_stream_key(
    const ValidatedRecognitionPacket& p) {
  return key_part(p.context_id) + key_part(p.document_ref) +
         key_part(p.document_generation);
}
std::string PacketAdmissionCoordinator::document_range_key(
    const ValidatedRecognitionPacket& p) {
  return std::to_string(p.document_sequence) + ":" +
         std::to_string(p.global_start_offset) + ":" +
         std::to_string(p.global_end_offset);
}

std::string PacketAdmissionCoordinator::encode(
    const ValidatedRecognitionPacket& p) {
  std::ostringstream out;
  out << "RPA3 " << std::quoted(p.packet_id) << ' '
      << std::quoted(p.idempotency_key) << ' ' << std::quoted(p.context_id) << ' '
      << std::quoted(p.source_id) << ' ' << std::quoted(p.source_generation) << ' '
      << std::quoted(p.block_ref) << ' ' << std::quoted(p.document_ref) << ' '
      << std::quoted(p.document_generation) << ' ' << p.document_sequence << ' '
      << p.global_start_offset << ' ' << p.global_end_offset << ' '
      << std::quoted(p.offset_basis) << ' ' << p.start_offset << ' '
      << p.end_offset << ' ' << p.source_sequence << ' '
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
  if (!(in >> magic) || (magic != "RPA1" && magic != "RPA2" && magic != "RPA3"))
    return false;
  if (!(in >> std::quoted(p.packet_id) >> std::quoted(p.idempotency_key)
        >> std::quoted(p.context_id) >> std::quoted(p.source_id)
        >> std::quoted(p.source_generation) >> std::quoted(p.block_ref)))
    return false;
  if (magic == "RPA3") {
    if (!(in >> std::quoted(p.document_ref) >> std::quoted(p.document_generation)
          >> p.document_sequence >> p.global_start_offset >> p.global_end_offset))
      return false;
  }
  if (!(in >> std::quoted(p.offset_basis) >> p.start_offset >> p.end_offset
        >> p.source_sequence)) return false;
  if ((magic == "RPA2" || magic == "RPA3") &&
      !(in >> std::quoted(p.source_snapshot_digest))) return false;
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
  if (!p.document_ref.empty()) {
    if (p.document_generation.empty() || p.document_sequence == 0 ||
        p.global_end_offset <= p.global_start_offset ||
        p.global_end_offset - p.global_start_offset != p.end_offset - p.start_offset)
      return false;
    auto& document = document_streams_[document_stream_key(p)];
    const auto drange = document_range_key(p);
    const auto prior = document.sequence_packets.find(p.document_sequence);
    if (p.revision) {
      const auto latest_document = document.range_latest_packets.find(drange);
      if (prior == document.sequence_packets.end() ||
          latest_document == document.range_latest_packets.end() ||
          prior->second != p.revision_of_packet_id ||
          latest_document->second != p.revision_of_packet_id) return false;
      prior->second = p.packet_id;
      latest_document->second = p.packet_id;
    } else {
      if (p.document_sequence != document.next_sequence ||
          p.global_start_offset != document.next_global_offset ||
          prior != document.sequence_packets.end()) return false;
      document.sequence_packets[p.document_sequence] = p.packet_id;
      document.range_latest_packets[drange] = p.packet_id;
      ++document.next_sequence;
      document.next_global_offset = p.global_end_offset;
    }
  } else if (!p.document_generation.empty() || p.document_sequence != 0 ||
             p.global_start_offset != 0 || p.global_end_offset != 0) {
    return false;
  }
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
  packet_encodings_[p.packet_id] = encode(p);
  idempotency_packets_[p.idempotency_key] = p.packet_id;
  return true;
}

bool PacketAdmissionCoordinator::recover() {
  recovery_failure_stage_.clear();
  recovery_failure_code_.clear();
  auto fail_recovery = [&](const std::string& code) {
    recovered_ = false;
    recovery_failure_stage_ = "JOURNAL_RECOVERY";
    recovery_failure_code_ = code;
    return false;
  };
  streams_.clear();
  document_streams_.clear();
  packet_digests_.clear();
  packet_encodings_.clear();
  idempotency_packets_.clear();
  const auto events = journal_.committed_events();
  if (journal_.state_version() == 0 && events.empty()) {
    recovered_ = true;
    return true;
  }

  std::uint64_t expected_version = journal_.checkpoint_info().base_state_version;
  std::size_t index = 0;
  while (index < events.size()) {
    const auto version = events[index].state_version;
    if (version == 0 || expected_version == std::numeric_limits<std::uint64_t>::max() ||
        version != expected_version + 1) {
      return fail_recovery("RECOVERY_EVENT_VERSION_INVALID");
    }
    expected_version = version;
    const auto begin = index;
    while (index < events.size() && events[index].state_version == version)
      ++index;

    std::size_t packet_index = index;
    std::size_t packet_count = 0;
    for (std::size_t i = begin; i < index; ++i) {
      if (events[i].kind == "RECOGNITION_PACKET_ADMITTED" ||
          events[i].kind == "SEMANTIC_PACKET_ADMITTED") {
        packet_index = i;
        ++packet_count;
      }
    }
    if (packet_count > 1) {
      return fail_recovery("RECOVERY_MULTIPLE_PACKET_ADMISSIONS");
    }
    if (packet_count == 0) continue;
    if (packet_index != begin) {
      return fail_recovery("RECOVERY_PACKET_ADMISSION_NOT_FIRST");
    }

    std::size_t delta_meta_count = 0;
    std::size_t delta_meta_index = index;
    for (std::size_t i = begin; i < index; ++i) {
      if (events[i].kind == "DELTA_META") {
        ++delta_meta_count;
        delta_meta_index = i;
      }
    }

    const auto& packet_event = events[packet_index];
    if (packet_event.kind == "SEMANTIC_PACKET_ADMITTED") {
      // Packet and semantic events are one Journal transaction. Recovery
      // rejects a truncated or reordered semantic sequence rather than
      // admitting the packet while silently losing its projection.
      if (delta_meta_count != 1 || delta_meta_index != packet_index + 1 ||
          packet_index + 2 >= index ||
          events[packet_index + 1].state_version != version ||
          events[packet_index + 2].state_version != version) {
        return fail_recovery("RECOVERY_SEMANTIC_EVENT_SEQUENCE_INCOMPLETE");
      }
    } else if (delta_meta_count != 0) {
      return fail_recovery("RECOVERY_UNEXPECTED_SEMANTIC_EVENTS");
    }

    ValidatedRecognitionPacket packet;
    if (!decode(packet_event.payload, packet) || !apply_recovered(packet)) {
      return fail_recovery("RECOVERY_PACKET_DECODE_OR_STATE_INVALID");
    }
  }
  if (expected_version != journal_.state_version())
    return fail_recovery("RECOVERY_JOURNAL_STATE_VERSION_MISMATCH");
  recovered_ = true;
  return true;
}

PacketAdmissionResult PacketAdmissionCoordinator::admit(
    const ValidatedRecognitionPacket& p,
    const std::string& normalized_source_snapshot,
    const std::vector<JournalEvent>& semantic_events) {
  PacketAdmissionResult result;
  PacketAdmissionFailureFinalizer failure_finalizer{result};
  result.state_version = journal_.state_version();
  if (!recovered_) { result.error = "PACKET_COORDINATOR_NOT_RECOVERED"; return result; }
   if (p.packet_id.empty() || p.idempotency_key.empty() || p.context_id.empty() ||
      p.source_id.empty() || p.source_generation.empty() || p.block_ref.empty() ||
      p.source_snapshot_digest.size() != 64 || p.source_range_digest.size() != 64 || p.canonical_request_digest.empty() ||
      p.offset_basis != "UTF8_BYTE_NFC" || p.end_offset <= p.start_offset ||
      p.source_sequence == 0 || p.source_sequence == std::numeric_limits<std::uint64_t>::max() ||
      (p.revision && (p.revision_sequence == 0 || p.revision_of_packet_id.empty())) ||
      (!p.revision && (p.revision_sequence != 0 || !p.revision_of_packet_id.empty())) ||
      (!p.document_ref.empty() &&
       (p.document_generation.empty() || p.document_sequence == 0 ||
        p.global_end_offset <= p.global_start_offset ||
        p.global_end_offset - p.global_start_offset != p.end_offset - p.start_offset)) ||
      (p.document_ref.empty() &&
       (!p.document_generation.empty() || p.document_sequence != 0 ||
        p.global_start_offset != 0 || p.global_end_offset != 0))) {
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
    if (!semantic_events.empty()) {
      result.error = "PACKET_ALREADY_ADMITTED_SEMANTIC_NOT_COMMITTED";
      return result;
    }
    const auto encoded = packet_encodings_.find(p.packet_id);
    if (existing_packet->second != p.canonical_request_digest ||
        encoded == packet_encodings_.end() || encoded->second != encode(p)) {
      result.error = "PACKET_ID_REUSED"; return result;
    }
    result.commit_state = AdmissionCommitState::Committed;
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
  DocumentStreamState staged_document;
  std::string dkey;
  std::string drange;
  if (!p.document_ref.empty()) {
    dkey = document_stream_key(p);
    auto old_document = document_streams_.find(dkey);
    if (old_document != document_streams_.end()) staged_document = old_document->second;
    drange = document_range_key(p);
    const auto prior = staged_document.sequence_packets.find(p.document_sequence);
    if (p.revision) {
      const auto latest_document = staged_document.range_latest_packets.find(drange);
      if (prior == staged_document.sequence_packets.end() ||
          latest_document == staged_document.range_latest_packets.end() ||
          latest_document->second != p.revision_of_packet_id ||
          prior->second != p.revision_of_packet_id) {
        result.error = "DOCUMENT_REVISION_TARGET_MISMATCH"; return result;
      }
    } else {
      if (p.document_sequence != staged_document.next_sequence) {
        result.error = p.document_sequence < staged_document.next_sequence
            ? "DOCUMENT_SEQUENCE_REUSED" : "DOCUMENT_SEQUENCE_GAP";
        return result;
      }
      if (p.global_start_offset != staged_document.next_global_offset) {
        result.error = p.global_start_offset < staged_document.next_global_offset
            ? "DOCUMENT_GLOBAL_RANGE_OVERLAP" : "DOCUMENT_GLOBAL_RANGE_GAP";
        return result;
      }
    }
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
  if (!p.document_ref.empty()) {
    staged_document.sequence_packets[p.document_sequence] = p.packet_id;
    staged_document.range_latest_packets[drange] = p.packet_id;
    if (!p.revision) {
      ++staged_document.next_sequence;
      staged_document.next_global_offset = p.global_end_offset;
    }
  }

  const auto expected = journal_.state_version();
  if (!semantic_events.empty()) {
    if (semantic_events.front().kind != "DELTA_META") {
      result.error = "SEMANTIC_TRANSACTION_META_REQUIRED"; return result;
    }
    std::istringstream meta(semantic_events.front().payload);
    std::string delta_id, semantic_key, fingerprint;
    std::uint64_t semantic_expected = 0;
    if (!(meta >> std::quoted(delta_id) >> std::quoted(semantic_key) >>
          semantic_expected >> std::quoted(fingerprint)) ||
        delta_id.empty() || semantic_key.empty() || fingerprint.empty()) {
      result.error = "SEMANTIC_TRANSACTION_META_INVALID"; return result;
    }
    if (semantic_expected != expected) {
      result.error = "STATE_VERSION_CONFLICT"; return result;
    }
  }
  const std::string tx = "recognition-packet-tx:" + p.packet_id;
  std::vector<JournalEvent> transaction_events;
  transaction_events.push_back({packet_event_id(p.packet_id),
      semantic_events.empty() ? "RECOGNITION_PACKET_ADMITTED" : "SEMANTIC_PACKET_ADMITTED",
      encode(p)});
  transaction_events.insert(transaction_events.end(), semantic_events.begin(), semantic_events.end());
  const auto committed = journal_.commit(tx, p.idempotency_key, expected, transaction_events);
  if (!committed.committed) {
    if (committed.error == "JOURNAL_COMMIT_OUTCOME_UNKNOWN")
      result.commit_state = AdmissionCommitState::CommitOutcomeUnknown;
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
  if (!p.document_ref.empty()) document_streams_[dkey] = std::move(staged_document);
  packet_digests_[p.packet_id] = p.canonical_request_digest;
  packet_encodings_[p.packet_id] = encode(p);
  idempotency_packets_[p.idempotency_key] = p.packet_id;
  result.commit_state = AdmissionCommitState::Committed;
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
std::size_t PacketAdmissionCoordinator::estimated_resident_bytes() const noexcept {
  std::size_t total = 0;
  auto add = [&](std::size_t value) {
    if (value > std::numeric_limits<std::size_t>::max() - total) {
      total = std::numeric_limits<std::size_t>::max();
      return false;
    }
    total += value;
    return true;
  };
  for (const auto& item : packet_digests_) {
    if (!add(256) || !add(item.first.size()) || !add(item.second.size())) return total;
  }
  for (const auto& item : packet_encodings_) {
    if (!add(256) || !add(item.first.size()) || !add(item.second.size())) return total;
  }
  for (const auto& item : idempotency_packets_) {
    if (!add(256) || !add(item.first.size()) || !add(item.second.size())) return total;
  }
  for (const auto& document : document_streams_) {
    if (!add(256) || !add(document.first.size()) ||
        !add(sizeof(document.second.next_sequence)) ||
        !add(sizeof(document.second.next_global_offset))) return total;
    for (const auto& item : document.second.sequence_packets)
      if (!add(96) || !add(sizeof(item.first)) || !add(item.second.size())) return total;
    for (const auto& item : document.second.range_latest_packets)
      if (!add(128) || !add(item.first.size()) || !add(item.second.size())) return total;
  }
  for (const auto& stream : streams_) {
    if (!add(256) || !add(stream.first.size())) return total;
    for (const auto& item : stream.second.sequence_packets)
      if (!add(96) || !add(sizeof(item.first)) || !add(item.second.size())) return total;
    for (const auto& item : stream.second.range_latest_packets)
      if (!add(128) || !add(item.first.size()) || !add(item.second.size())) return total;
  }
  return total;
}
bool PacketAdmissionCoordinator::contains_packet(const std::string& packet_id) const {
  return packet_digests_.find(packet_id) != packet_digests_.end();
}
}  // namespace surge
