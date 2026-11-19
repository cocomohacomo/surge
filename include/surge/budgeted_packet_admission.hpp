#pragma once

#include "surge/packet_admission.hpp"
#include "surge/resource_budget.hpp"

#include <map>
#include <string>
#include <utility>

namespace surge {

// Production admission facade: reserves transient capacity before validation /
// staging, and resident + retained-event capacity before the durable commit.
// ResourceBudget must outlive this object and all leases it owns.
class BudgetedPacketAdmissionCoordinator {
 public:
  BudgetedPacketAdmissionCoordinator(EventJournal& journal, ResourceBudget& budget);
  bool recover(std::string& error);
  PacketAdmissionResult admit(const ValidatedRecognitionPacket& packet,
      const std::string& normalized_source_snapshot,
      const std::vector<JournalEvent>& semantic_events = {});
  std::size_t retained_packet_count() const noexcept { return retained_.size(); }

 private:
  static bool checked_add(std::size_t& total, std::size_t value) noexcept;
  static bool packet_resident_cost(const ValidatedRecognitionPacket& packet,
                                   std::size_t& bytes) noexcept;
  bool retain(const std::string& event_id, std::size_t resident_bytes,
              ResourceLease resident, ResourceLease event, std::string& error);

  EventJournal& journal_;
  ResourceBudget& budget_;
  PacketAdmissionCoordinator coordinator_;
  std::map<std::string, std::pair<ResourceLease, ResourceLease>> retained_;
};

}  // namespace surge
