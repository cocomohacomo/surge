#pragma once
#include "surge/event_journal.hpp"
#include "surge/resource_budget.hpp"
#include <map>
#include <string>
#include <utility>
#include <vector>
namespace surge {
// Budget-enforced write path for EventJournal. Use this facade for all runtime
// commits; direct EventJournal access remains available for recovery/verification.
class BudgetedEventJournal {
 public:
  BudgetedEventJournal(EventJournal& journal, ResourceBudget& budget);
  bool recover(std::string& error);
  JournalCommitResult commit(const std::string& transaction_id,
      const std::string& idempotency_key, std::uint64_t expected_state_version,
      const std::vector<JournalEvent>& events);
  std::size_t retained_event_count() const noexcept { return retained_.size(); }
 private:
  static bool add(std::size_t& total, std::size_t value) noexcept;
  EventJournal& journal_;
  ResourceBudget& budget_;
  std::map<std::string, std::pair<ResourceLease, ResourceLease>> retained_;
};
} // namespace surge
