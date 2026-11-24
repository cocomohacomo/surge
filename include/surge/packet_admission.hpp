#pragma once

#include "surge/core.hpp"
#include "surge/event_journal.hpp"
#include "surge/sha256.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace surge {

// Typed envelope from the adapter. Core independently verifies the exact
// NFC UTF-8 snapshot and its source range before admission.
struct ValidatedRecognitionPacket {
  std::string packet_id;
  std::string idempotency_key;
  std::string context_id;
  std::string source_id;
  std::string source_generation;
  std::string block_ref;
  // Immutable document-wide stream identity and UTF-8 byte coordinates.
  std::string document_ref;
  std::string document_generation;
  std::uint64_t document_sequence{};
  std::uint64_t global_start_offset{};
  std::uint64_t global_end_offset{};
  std::string offset_basis{"UTF8_BYTE_NFC"};
  std::uint64_t start_offset{};
  std::uint64_t end_offset{};
  std::uint64_t source_sequence{};
  std::string source_snapshot_digest;
  std::string source_range_digest;
  std::string canonical_request_digest;
  std::string revision_of_packet_id;
  bool revision{false};
  std::uint64_t revision_sequence{};
};

enum class AdmissionFailureStage { None, Recovery, PacketValidation, SourceValidation, Idempotency, SequenceValidation, SemanticValidation, ProjectionValidation, ProjectionBinding, GraphPreparation, JournalCommit, GraphReplay, StateConsistency };

struct PacketAdmissionResult {
  bool admitted{false};
  bool duplicate{false};
  std::uint64_t state_version{};
  std::uint64_t watermark{};
  std::string packet_status;
  std::string error;
};

// Coordinates SourceSequencer + RangeLedger + EventJournal. In-memory state
// is staged first and published only after the journal's commit marker succeeds.
class PacketAdmissionCoordinator {
 public:
  explicit PacketAdmissionCoordinator(EventJournal& journal);
  bool recover();
  PacketAdmissionResult admit(const ValidatedRecognitionPacket& packet,
                              const std::string& normalized_source_snapshot,
                              const std::vector<JournalEvent>& semantic_events = {});
  std::uint64_t state_version() const;
  std::size_t packet_count() const;
  std::size_t estimated_resident_bytes() const noexcept;
  bool contains_packet(const std::string& packet_id) const;
  const EventJournal& journal() const noexcept { return journal_; }\n  const EventJournal& journal() const noexcept { return journal_; }

 private:
  struct DocumentStreamState {
    std::map<std::uint64_t, std::string> sequence_packets;
    std::map<std::string, std::string> range_latest_packets;
    std::uint64_t next_sequence{1};
    std::uint64_t next_global_offset{0};
  };
  struct StreamState {
    SourceSequencer sequencer;
    RangeLedger ranges;
    std::map<std::uint64_t, std::string> sequence_packets;
    std::map<std::string, std::string> range_latest_packets;
  };
  static std::string stream_key(const ValidatedRecognitionPacket& packet);
  static std::string range_key(const ValidatedRecognitionPacket& packet);
  static std::string document_stream_key(const ValidatedRecognitionPacket& packet);
  static std::string document_range_key(const ValidatedRecognitionPacket& packet);
  static std::string encode(const ValidatedRecognitionPacket& packet);
  static bool decode(const std::string& payload, ValidatedRecognitionPacket& packet);
  bool apply_recovered(const ValidatedRecognitionPacket& packet);

  EventJournal& journal_;
  std::map<std::string, StreamState> streams_;
  std::map<std::string, DocumentStreamState> document_streams_;
  std::map<std::string, std::string> packet_digests_;
  // Compare the complete durable packet envelope as well as the caller digest.
  // The digest is an optimization/integrity field, not an identity authority.
  std::map<std::string, std::string> packet_encodings_;
  std::map<std::string, std::string> idempotency_packets_;
  bool recovered_{false};
};
}  // namespace surge
