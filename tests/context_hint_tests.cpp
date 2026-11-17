#include "test_support.hpp"
#include "surge/context_hint.hpp"
#include "surge/context_graph.hpp"
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
  CHECK(built.error.empty() && built.ticket_required);
  CHECK(built.hint.included_refs.size()==3);
  CHECK(built.hint.included_refs[0]=="scope-1");
  CHECK(built.hint.included_refs[1]=="qa-1");
  CHECK(built.hint.included_refs[2]=="dep-1");
  CHECK(!built.hint.context_complete && !built.hint.retrieval_ticket_id.empty());
  CHECK(built.ticket.omitted_refs.size()==2);
  CHECK(built.ticket.omitted_refs[0]=="lineage-1" && built.ticket.omitted_refs[1]=="focus-1");
  RetrievalTicketRegistry registry;
  CHECK(registry.issue(built.ticket));
  CHECK(registry.issue(built.ticket) && registry.size()==1);
  auto same=registry.resolve(built.ticket.ticket_id,7,false);
  CHECK(same.status==TicketStatus::Resolved && same.refs==built.ticket.omitted_refs);
  auto stale=registry.resolve(built.ticket.ticket_id,8,false);
  CHECK(stale.status==TicketStatus::Stale);
  auto reevaluated=registry.resolve(built.ticket.ticket_id,8,true,{"new-ref","new-ref","newer-ref"});
  CHECK(reevaluated.status==TicketStatus::Resolved && reevaluated.refs.size()==2);
  auto repeat=registry.resolve(built.ticket.ticket_id,8,true,{"ignored"});
  CHECK(repeat.refs==reevaluated.refs);

  std::string error;
  const auto journal_path=(std::filesystem::temp_directory_path()/"surge-persistent-ticket-tests.sgj").string();
  std::filesystem::remove(journal_path);
  std::string persistent_ticket_id=built.ticket.ticket_id;
  {
    EventJournal journal(journal_path);
    CHECK(journal.open());
    ContextGraph graph(journal);
    CHECK(graph.open());
    PersistentRetrievalTicketRegistry durable(journal);
    CHECK(durable.open());
    auto ticket=built.ticket;
    ticket.state_version=graph.state_version();
    CHECK(durable.issue(ticket,error));
    const auto after_issue=graph.state_version();
    CHECK(after_issue==1);
    CHECK(durable.issue(ticket,error));
    CHECK(graph.state_version()==after_issue);
    auto resolved=durable.resolve(ticket.ticket_id,ticket.state_version,false,{},error);
    CHECK(error.empty()&&resolved.status==TicketStatus::Resolved);
    CHECK(resolved.refs==ticket.omitted_refs);
    CHECK(graph.state_version()==2);
  }
  {
    EventJournal journal(journal_path);
    CHECK(journal.open());
    ContextGraph graph(journal);
    CHECK(graph.open());
    CHECK(graph.state_version()==2);
    PersistentRetrievalTicketRegistry durable(journal);
    CHECK(durable.open()&&durable.size()==1);
    auto recovered=durable.resolve(persistent_ticket_id,0,false,{},error);
    CHECK(error.empty()&&recovered.status==TicketStatus::Resolved);
    CHECK(recovered.refs==built.ticket.omitted_refs);
    auto stale=durable.resolve(persistent_ticket_id,2,false,{},error);
    CHECK(error.empty()&&stale.status==TicketStatus::Stale);
    auto reevaluated=durable.resolve(persistent_ticket_id,2,true,{"new-a","new-a","new-b"},error);
    CHECK(error.empty()&&reevaluated.status==TicketStatus::Resolved&&reevaluated.refs.size()==2);
  }
  std::filesystem::remove(journal_path);

  auto invalid=ContextHintBuilder::build("","packet",digest,"p",0,candidates,1);
  CHECK(invalid.error=="CONTEXT_HINT_REQUEST_INVALID");
  auto optional=ContextHintBuilder::build("ctx","packet-2",digest,"policy-1",8,
      {{"scope",HintClass::OpenScope,0,0},{"cluster",HintClass::RelatedCluster,0,0}},1);
  CHECK(optional.error.empty()&&!optional.ticket_required&&optional.hint.context_complete);
  std::cout<<"SURGE ContextHint tests: 7 groups passed\n";
}
