#include "surge/budgeted_event_journal.hpp"
#include <limits>
namespace surge {
BudgetedEventJournal::BudgetedEventJournal(EventJournal& journal, ResourceBudget& budget)
    : journal_(journal), budget_(budget) {}
bool BudgetedEventJournal::add(std::size_t& total, std::size_t value) noexcept {
  if (value > std::numeric_limits<std::size_t>::max() - total) return false;
  total += value; return true;
}
bool BudgetedEventJournal::recover(std::string& error) {
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
  JournalCommitResult failed; failed.state_version=journal_.state_version();
  if(events.empty()){failed.error="JOURNAL_EMPTY_TRANSACTION";return failed;}
  std::size_t transient_bytes=128,resident_bytes=256;
  for(const auto& e:events){
    if(e.event_id.empty() || retained_.count(e.event_id)){failed.error="JOURNAL_EVENT_ID_ALREADY_RETAINED";return failed;}
    if(!add(transient_bytes,e.event_id.size())||!add(transient_bytes,e.kind.size())||
       !add(transient_bytes,e.payload.size())||!add(resident_bytes,e.event_id.size())||
       !add(resident_bytes,e.kind.size())||!add(resident_bytes,e.payload.size())){
      failed.error="JOURNAL_RESOURCE_ACCOUNTING_OVERFLOW";return failed;
    }
  }
  ResourceLease transient,resident;
  std::string error;
  if(!budget_.try_acquire(ResourceKind::TransientBytes,transient_bytes,transient,error)){
    failed.error="JOURNAL_TRANSIENT_"+error;return failed;
  }
  if(!budget_.try_acquire(ResourceKind::ResidentBytes,resident_bytes,resident,error)){
    failed.error="JOURNAL_RESIDENT_"+error;return failed;
  }
  std::vector<ResourceLease> event_leases;
  event_leases.reserve(events.size());
  for(std::size_t i=0;i<events.size();++i){
    ResourceLease count;
    if(!budget_.try_acquire(ResourceKind::RetainedEvents,1,count,error)){
      failed.error="JOURNAL_EVENT_"+error;return failed;
    }
    event_leases.push_back(std::move(count));
  }
  auto result=journal_.commit(tx,idem,expected,events);
  if(!result.committed || result.duplicate)return result;
  // The transaction's resident reservation is held once and split logically
  // across event entries via shared transaction ownership represented by first.
  // Each event retains a count lease; resident lease is held by first event.
  for(std::size_t i=0;i<events.size();++i){
    ResourceLease bytes;
    if(i==0) bytes=std::move(resident);
    retained_.emplace(events[i].event_id,
      std::make_pair(std::move(bytes),std::move(event_leases[i])));
  }
  return result;
}
} // namespace surge
