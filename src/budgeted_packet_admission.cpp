#include "surge/budgeted_packet_admission.hpp"

#include <limits>
#include <utility>

namespace surge {

BudgetedPacketAdmissionCoordinator::BudgetedPacketAdmissionCoordinator(
    EventJournal& journal, ResourceBudget& budget)
    : journal_(journal), budget_(budget), coordinator_(journal) {}

bool BudgetedPacketAdmissionCoordinator::checked_add(
    std::size_t& total, std::size_t value) noexcept {
  if (value > std::numeric_limits<std::size_t>::max() - total) return false;
  total += value;
  return true;
}

bool BudgetedPacketAdmissionCoordinator::packet_resident_cost(
    const ValidatedRecognitionPacket& p, std::size_t& bytes) noexcept {
  bytes = 256;  // conservative map/node/object overhead allowance
  return checked_add(bytes, p.packet_id.size()) &&
      checked_add(bytes, p.idempotency_key.size()) &&
      checked_add(bytes, p.context_id.size()) &&
      checked_add(bytes, p.source_id.size()) &&
      checked_add(bytes, p.source_generation.size()) &&
      checked_add(bytes, p.block_ref.size()) &&
      checked_add(bytes, p.offset_basis.size()) &&
      checked_add(bytes, p.source_snapshot_digest.size()) &&
      checked_add(bytes, p.source_range_digest.size()) &&
      checked_add(bytes, p.canonical_request_digest.size()) &&
      checked_add(bytes, p.revision_of_packet_id.size());
}

bool BudgetedPacketAdmissionCoordinator::retain(
    const std::string& event_id, std::size_t,
    ResourceLease resident, ResourceLease event, std::string& error) {
  if (!resident || !event || event_id.empty()) {
    error = "PACKET_RESOURCE_LEASE_INVALID";
    return false;
  }
  const auto inserted = retained_.emplace(
      event_id, std::make_pair(std::move(resident), std::move(event)));
  if (!inserted.second) {
    error = "PACKET_RESOURCE_ID_ALREADY_RETAINED";
    return false;
  }
  return true;
}

bool BudgetedPacketAdmissionCoordinator::recover(std::string& error) {
  error.clear();
  retained_.clear();
  if (!coordinator_.recover()) {
    error = "PACKET_COORDINATOR_RECOVERY_FAILED";
    return false;
  }
  for (const auto& event : journal_.committed_events()) {
    if (event.kind != "RECOGNITION_PACKET_ADMITTED" &&
        event.kind != "SEMANTIC_PACKET_ADMITTED") continue;
    std::size_t cost = 256;
    if (!checked_add(cost, event.event_id.size()) ||
        !checked_add(cost, event.payload.size())) {
      error = "PACKET_RESOURCE_ACCOUNTING_OVERFLOW";
      retained_.clear();
      return false;
    }
    ResourceLease resident, retained_event;
    if (!budget_.try_acquire(ResourceKind::ResidentBytes, cost, resident, error) ||
        !budget_.try_acquire(ResourceKind::RetainedEvents, 1, retained_event, error) ||
        !retain(event.event_id, cost, std::move(resident),
                std::move(retained_event), error)) {
      retained_.clear();
      return false;
    }
  }
  return true;
}

PacketAdmissionResult BudgetedPacketAdmissionCoordinator::admit(
    const ValidatedRecognitionPacket& packet,
    const std::string& normalized_source_snapshot,
    const std::vector<JournalEvent>& semantic_events) {
  PacketAdmissionResult rejected;
  rejected.state_version = journal_.state_version();

  const auto event_id = "recognition-packet:" + packet.packet_id;
  // Idempotent retries consume no new retention capacity. The coordinator
  // still verifies that the complete packet envelope matches the durable one.
  if (retained_.find(event_id) != retained_.end())
    return coordinator_.admit(packet, normalized_source_snapshot, semantic_events);

  std::size_t transient_cost = 128;
  std::size_t packet_cost = 0;
  if (!packet_resident_cost(packet, packet_cost) ||
      !checked_add(transient_cost, packet_cost) ||
      !checked_add(transient_cost, normalized_source_snapshot.size())) {
    rejected.error = "PACKET_RESOURCE_ACCOUNTING_OVERFLOW";
    return rejected;
  }
  for (const auto& event : semantic_events) {
    if (!checked_add(transient_cost, event.event_id.size()) ||
        !checked_add(transient_cost, event.kind.size()) ||
        !checked_add(transient_cost, event.payload.size())) {
      rejected.error = "PACKET_RESOURCE_ACCOUNTING_OVERFLOW";
      return rejected;
    }
  }
  std::size_t resident_cost = 0;
  if (!packet_resident_cost(packet, resident_cost)) {
    rejected.error = "PACKET_RESOURCE_ACCOUNTING_OVERFLOW";
    return rejected;
  }

  ResourceLease transient, resident, retained_event;
  std::string error;
  if (!budget_.try_acquire(ResourceKind::TransientBytes, transient_cost,
                           transient, error)) {
    rejected.error = "PACKET_TRANSIENT_" + error;
    return rejected;
  }

  // Reserve retained capacity before the journal commit. A budget failure must
  // never leave a durable packet that the in-memory coordinator cannot retain.
  if (!budget_.try_acquire(ResourceKind::ResidentBytes, resident_cost,
                           resident, error)) {
    rejected.error = "PACKET_RESIDENT_" + error;
    return rejected;
  }
  if (!budget_.try_acquire(ResourceKind::RetainedEvents, 1,
                           retained_event, error)) {
    rejected.error = "PACKET_EVENT_" + error;
    return rejected;
  }

  auto result = coordinator_.admit(packet, normalized_source_snapshot, semantic_events);
  if (!result.admitted || result.duplicate) return result;

  if (!retain(event_id, resident_cost, std::move(resident),
              std::move(retained_event), error)) {
    // The reservation and identifier are prepared before commit, so this path
    // indicates an internal invariant violation rather than ordinary pressure.
    result.admitted = false;
    result.error = "PACKET_RESOURCE_RETENTION_INVARIANT_FAILED";
  }
  return result;
}

}  // namespace surge
