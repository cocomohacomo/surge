#include "surge/budgeted_event_journal.hpp"
#include <limits>
#include <set>
namespace surge {
BudgetedEventJournal::BudgetedEventJournal(EventJournal& journal, ResourceBudget& budget)
    : journal_(journal), budget_(budget) {}
bool BudgetedEventJournal::add(std::size_t& total, std::size_t value) noexcept {
  if (value > std::numeric_limits<std::size_t>::max() - total) return false;
  total += value; return true;
}
bool BudgetedEventJournal::recover(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear(); retained_.clear();
  for (const auto& e : journal_.committed_events()) {
    std::size_t bytes=256;
    if (!add(bytes,e.event_id.size()) || !add(bytes,e.kind.size()) || !add(bytes,e.payload.size())) {
      error="JOURNAL_RESOURCE_ACCOUNTING_OVERFLOW"; retained_.clear(); return false;
    }
    ResourceLease resident, count;
    if (!budget_.try_acquire(ResourceKind::ResidentBytes,bytes,resident,error) ||
        !budget_.try_acquire(ResourceKind::RetainedEvents,1,count,error)) {
      retained_.clear(); return false;
    }
    if (!retained_.emplace(e.event_id,std::make_pair(std::move(resident),std::move(count))).second) {
      error="JOURNAL_DUPLICATE_EVENT_ID_DURING_RECOVERY"; retained_.clear(); return false;
    }
  }
  return true;
}
JournalCommitResult BudgetedEventJournal::commit(const std::string& tx,
    const std::string& idem,std::uint64_t expected,const std::vector<JournalEvent>& events) {
  // Serialize admission, durable commit, and lease publication. Without this lock,
  // concurrent callers can both pass the retained-ID check and race map::merge.
  std::lock_guard<std::mutex> lock(mutex_);
  JournalCommitResult failed; failed.state_version=journal_.state_version();
  if(events.empty()){failed.error="JOURNAL_EMPTY_TRANSACTION";return failed;}
  // An older watermark cannot create a new transaction. Resolve durable
  // idempotency before retained-ID checks so retries consume no new budget.
  if (expected < failed.state_version)
    return journal_.commit(tx, idem, expected, events);
  std::size_t transient_bytes=128;
  std::vector<std::size_t> resident_event_bytes;
  resident_event_bytes.reserve(events.size());
  std::set<std::string> transaction_event_ids;
  for(const auto& e:events){
    if(e.event_id.empty() || retained_.count(e.event_id) ||
       !transaction_event_ids.insert(e.event_id).second){failed.error="JOURNAL_EVENT_ID_ALREADY_RETAINED";return failed;}
    std::size_t event_bytes=256;
    if(!add(transient_bytes,e.event_id.size())||!add(transient_bytes,e.kind.size())||
       !add(transient_bytes,e.payload.size())||!add(event_bytes,e.event_id.size())||
       !add(event_bytes,e.kind.size())||!add(event_bytes,e.payload.size())){
      failed.error="JOURNAL_RESOURCE_ACCOUNTING_OVERFLOW";return failed;
    }
    resident_event_bytes.push_back(event_bytes);
  }
  ResourceLease transient;
  std::string error;
  if(!budget_.try_acquire(ResourceKind::TransientBytes,transient_bytes,transient,error)){
    failed.error="JOURNAL_TRANSIENT_"+error;return failed;
  }
  // Reserve resident bytes and retained-event slots independently for every
  // event. This keeps lease ownership aligned with the event whose lifetime
  // it accounts for, including future per-event release/compaction paths.
  std::map<std::string, std::pair<ResourceLease, ResourceLease>> staged;
  for(std::size_t i=0;i<events.size();++i){
    ResourceLease bytes,count;
    if(!budget_.try_acquire(ResourceKind::ResidentBytes,resident_event_bytes[i],bytes,error)){
      failed.error="JOURNAL_RESIDENT_"+error;return failed;
    }
    if(!budget_.try_acquire(ResourceKind::RetainedEvents,1,count,error)){
      failed.error="JOURNAL_EVENT_"+error;return failed;
    }
    staged.emplace(events[i].event_id,
      std::make_pair(std::move(bytes),std::move(count)));
  }
  // All map nodes and budget leases exist before durable commit. merge() then
  // transfers preallocated nodes without allocating after the journal commits.
  auto result=journal_.commit(tx,idem,expected,events);
  if(!result.committed || result.duplicate)return result;
  retained_.merge(staged);
  if(!staged.empty()) {
    result.committed=false;
    result.error="JOURNAL_RESOURCE_RETENTION_INVARIANT_FAILED";
  }
  return result;
}
} // namespace surge
