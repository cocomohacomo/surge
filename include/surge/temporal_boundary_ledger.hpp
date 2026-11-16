#pragma once

#include "surge/event_journal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace surge {

struct TemporalBoundaryContribution {
  std::string claim_id;
  std::string role;  // VALID_FROM, VALID_UNTIL, or INTERVAL
  std::string valid_from;
  std::string valid_until;
  std::string evidence_id;
  std::string packet_id;
};

struct TemporalBoundaryPending {
  std::string claim_id;
  std::string valid_from;
  std::string valid_until;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> packet_ids;
};

struct TemporalBoundaryLedgerResult {
  bool committed{false};
  bool duplicate{false};
  std::uint64_t state_version{0};
  std::string status;
  std::string error;
  TemporalBoundaryPending pending;
};

// Durable proposal ledger layered on the same EventJournal instance used by
// the caller. It never writes ClaimValidity or performs temporal approval.
class TemporalBoundaryLedger {
 public:
  explicit TemporalBoundaryLedger(EventJournal& journal, std::size_t max_claims = 4096);

  bool replay();
  TemporalBoundaryLedgerResult add(const std::string& transaction_id,
                                   const std::string& idempotency_key,
                                   const std::string& event_id,
                                   std::uint64_t expected_state_version,
                                   const TemporalBoundaryContribution& contribution);
  TemporalBoundaryLedgerResult clear(const std::string& transaction_id,
                                     const std::string& idempotency_key,
                                     const std::string& event_id,
                                     std::uint64_t expected_state_version,
                                     const std::string& claim_id);
  bool get(const std::string& claim_id, TemporalBoundaryPending& output) const;
  std::size_t pending_claim_count() const;
  std::string last_error() const;

 private:
  bool apply_event(const JournalEvent& event);
  bool validate_contribution(const TemporalBoundaryContribution& contribution,
                             std::string& status) const;
  TemporalBoundaryLedgerResult result_for(const std::string& claim_id,
                                          const JournalCommitResult& commit) const;

  EventJournal& journal_;
  const std::size_t max_claims_;
  std::map<std::string, TemporalBoundaryPending> pending_;
  std::string last_error_;
};

}  // namespace surge
