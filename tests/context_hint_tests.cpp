#include "surge/context_hint.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>
using namespace surge;
int main(){
  const std::string digest(64,'b');
  std::vector<HintCandidate> candidates={
    {"cluster-z",HintClass::RelatedCluster,0,0},
    {"dep-1",HintClass::TriggeredDependency,2,1},
    {"scope-1",HintClass::OpenScope,1,0},
    {"qa-1",HintClass::RequiredQuestionAspect,0,0},
    {"lineage-1",HintClass::RequiredLineage,0,0},
    {"focus-1",HintClass::ActiveFocusScope,0,0},
    {"old-1",HintClass::HistoricalAnalogue,0,0},
    {"optional-1",HintClass::LowPriority,0,0},
    {"scope-1",HintClass::OpenScope,4,0}
  };
  auto built=ContextHintBuilder::build("ctx","packet-1",digest,"policy-1",7,candidates,3);
  assert(built.error.empty() && built.ticket_required);
  assert(built.hint.included_refs.size()==3);
  assert(built.hint.included_refs[0]=="scope-1");
  assert(built.hint.included_refs[1]=="qa-1");
  assert(built.hint.included_refs[2]=="dep-1");
  assert(!built.hint.context_complete && !built.hint.retrieval_ticket_id.empty());
  assert(built.ticket.omitted_refs.size()==2);
  assert(built.ticket.omitted_refs[0]=="lineage-1" && built.ticket.omitted_refs[1]=="focus-1");
  RetrievalTicketRegistry registry;
  assert(registry.issue(built.ticket));
  assert(registry.issue(built.ticket) && registry.size()==1);
  auto same=registry.resolve(built.ticket.ticket_id,7,false);
  assert(same.status==TicketStatus::Resolved && same.refs==built.ticket.omitted_refs);
  auto stale=registry.resolve(built.ticket.ticket_id,8,false);
  assert(stale.status==TicketStatus::Stale);
  auto reevaluated=registry.resolve(built.ticket.ticket_id,8,true,{"new-ref","new-ref","newer-ref"});
  assert(reevaluated.status==TicketStatus::Resolved && reevaluated.refs.size()==2);
  auto repeat=registry.resolve(built.ticket.ticket_id,8,true,{"ignored"});
  assert(repeat.refs==reevaluated.refs);

  const auto journal_path=(std::filesystem::temp_directory_path()/"surge-persistent-ticket-tests.sgj").string();
  std::filesystem::remove(journal_path);
  std::string persistent_ticket_id=built.ticket.ticket_id;
  {
    EventJournal journal(journal_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    PersistentRetrievalTicketRegistry durable(journal);
    assert(durable.open());
    auto ticket=built.ticket;
    ticket.state_version=graph.state_version();
    assert(durable.issue(ticket,error));
    const auto after_issue=graph.state_version();
    assert(after_issue==1);
    assert(durable.issue(ticket,error));
    assert(graph.state_version()==after_issue);
    auto resolved=durable.resolve(ticket.ticket_id,ticket.state_version,false,{},error);
    assert(error.empty()&&resolved.status==TicketStatus::Resolved);
    assert(resolved.refs==ticket.omitted_refs);
    assert(graph.state_version()==2);
  }
  {
    EventJournal journal(journal_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    assert(graph.state_version()==2);
    PersistentRetrievalTicketRegistry durable(journal);
    assert(durable.open()&&durable.size()==1);
    auto recovered=durable.resolve(persistent_ticket_id,0,false,{},error);
    assert(error.empty()&&recovered.status==TicketStatus::Resolved);
    assert(recovered.refs==built.ticket.omitted_refs);
    auto stale=durable.resolve(persistent_ticket_id,2,false,{},error);
    assert(error.empty()&&stale.status==TicketStatus::Stale);
    auto reevaluated=durable.resolve(persistent_ticket_id,2,true,{"new-a","new-a","new-b"},error);
    assert(error.empty()&&reevaluated.status==TicketStatus::Resolved&&reevaluated.refs.size()==2);
  }
  std::filesystem::remove(journal_path);

  auto invalid=ContextHintBuilder::build("","packet",digest,"p",0,candidates,1);
  assert(invalid.error=="CONTEXT_HINT_REQUEST_INVALID");
  auto optional=ContextHintBuilder::build("ctx","packet-2",digest,"policy-1",8,
      {{"scope",HintClass::OpenScope,0,0},{"cluster",HintClass::RelatedCluster,0,0}},1);
  assert(optional.error.empty()&&!optional.ticket_required&&optional.hint.context_complete);
  std::cout<<"SURGE ContextHint tests: 7 groups passed\n";
}
