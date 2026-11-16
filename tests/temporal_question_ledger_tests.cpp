#include "surge/temporal_boundary_ledger.hpp"
#include "surge/temporal_question_ledger.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace surge;
int main(){
 const auto path=std::filesystem::temp_directory_path()/"surge-temporal-question-tests.sgj";
 std::filesystem::remove(path);
 {
  EventJournal j(path.string());assert(j.open());TemporalBoundaryLedger b(j);assert(b.replay());
  TemporalQuestionLedger q(j);assert(q.replay());
  auto a=b.add("btx1","bidem1","b-event-1",0,{"claim/1","VALID_FROM","2026-01-01T00:00:00+09:00","","evidence/1","packet/1"});
  assert(a.committed);
  auto c=b.add("btx2","bidem2","b-event-2",1,{"claim/1","VALID_FROM","2026-02-01T00:00:00+09:00","","evidence/2","packet/2"});
  assert(c.committed&&c.status=="BOUNDARY_CONFLICT_REVIEW_REQUIRED");
  auto opened=q.synchronize("qtx1","qidem1","q-event-1",2,c.pending);
  assert(opened.committed&&opened.status=="OPEN"&&opened.question.candidate_ids.size()==2);
  assert(opened.question.evidence_ids.size()==2&&opened.question.packet_ids.size()==2);
  auto retry=q.synchronize("qtx1","qidem1","q-event-1",2,c.pending);assert(retry.committed&&retry.duplicate);
  auto s=b.resolve_candidate("btx3","bidem3","b-event-3",3,"claim/1","VALID_FROM","b-event-1");
  assert(s.committed);auto closed=q.synchronize("qtx2","qidem2","q-event-2",4,s.pending);
  assert(closed.committed&&closed.status=="RESOLVED_PENDING_APPROVAL");
  auto n=b.add("btx4","bidem4","b-event-4",5,{"claim/1","VALID_FROM","2026-03-01T00:00:00+09:00","","evidence/3","packet/3"});
  assert(n.committed);auto reopened=q.synchronize("qtx3","qidem3","q-event-3",6,n.pending);
  assert(reopened.committed&&reopened.status=="REOPENED"&&reopened.question.candidate_ids.size()==3);
 }
 {
  EventJournal j(path.string());assert(j.open());TemporalQuestionLedger q(j);assert(q.replay());
  TemporalQuestion v;assert(q.get("claim/1","VALID_FROM",v));assert(v.status=="REOPENED");
  assert(v.candidate_ids.size()==3&&v.evidence_ids.size()==3&&v.packet_ids.size()==3);
 }
 std::filesystem::remove(path);std::cout<<"SURGE TemporalQuestionLedger lifecycle and replay passed\n";
}
