#include "test_support.hpp"
#include "surge/budgeted_event_journal.hpp"
#include <filesystem>
using namespace surge;
int main(){
 const auto root=std::filesystem::temp_directory_path()/"surge-budgeted-journal";
 std::filesystem::remove_all(root); const auto path=(root/"events.sgj").string();
 {
  EventJournal journal(path); CHECK(journal.open());
  ResourceBudget budget({4096,4096,4,4,16,64});
  BudgetedEventJournal bounded(journal,budget); std::string error;
  CHECK(bounded.recover(error));
  std::vector<JournalEvent> events{{"e1","CLAIM","payload"},{"e2","EVIDENCE","payload"}};
  auto committed=bounded.commit("tx1","idem1",0,events);
  CHECK(committed.committed&&!committed.duplicate);
  CHECK(bounded.retained_event_count()==2);
  CHECK(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::RetainedEvents)]==2);
  auto duplicate=bounded.commit("tx1","idem1",0,events);
  CHECK(duplicate.duplicate);
 }
 {
  EventJournal journal(path); CHECK(journal.open());
  ResourceBudget budget({4096,4096,4,4,16,64});
  BudgetedEventJournal bounded(journal,budget); std::string error;
  CHECK(bounded.recover(error)); CHECK(bounded.retained_event_count()==2);
 }
 {
  EventJournal journal((root/"over.sgj").string()); CHECK(journal.open());
  ResourceBudget budget({4096,4096,1,4,16,64});
  BudgetedEventJournal bounded(journal,budget); std::string error;
  CHECK(bounded.recover(error));
  std::vector<JournalEvent> events{{"e1","CLAIM","payload"},{"e2","EVIDENCE","payload"}};
  auto rejected=bounded.commit("tx","idem",0,events);
  CHECK(!rejected.committed&&rejected.error=="JOURNAL_EVENT_RESOURCE_BUDGET_EXCEEDED");
  CHECK(journal.state_version()==0);
 }
 std::filesystem::remove_all(root); return 0;
}
