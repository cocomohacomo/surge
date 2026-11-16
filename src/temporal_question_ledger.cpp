#include "surge/temporal_question_ledger.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
namespace surge { namespace {
constexpr char kKind[]="TEMPORAL_QUESTION_SNAPSHOT_V1";
std::string qid(const std::string& c,const std::string& r){return "question/temporal/"+c+"/"+r;}
std::string encode_one(const TemporalQuestion&q){
 std::ostringstream o;o<<"1 "<<std::quoted(q.question_id)<<' '<<std::quoted(q.claim_id)<<' '
 <<std::quoted(q.role)<<' '<<std::quoted(q.status)<<' '<<std::quoted(q.last_event_id)<<' '<<q.candidate_ids.size();
 for(const auto&v:q.candidate_ids)o<<' '<<std::quoted(v);
 o<<' '<<q.evidence_ids.size();for(const auto&v:q.evidence_ids)o<<' '<<std::quoted(v);
 o<<' '<<q.packet_ids.size();for(const auto&v:q.packet_ids)o<<' '<<std::quoted(v);
 return o.str();
}
bool decode_one(const std::string&p,TemporalQuestion&q){
 std::istringstream i(p);int ver=0;std::size_t n=0;
 if(!(i>>ver>>std::quoted(q.question_id)>>std::quoted(q.claim_id)>>std::quoted(q.role)>>
   std::quoted(q.status)>>std::quoted(q.last_event_id)>>n)||ver!=1||q.question_id.empty()||
   q.claim_id.empty()||q.role.empty()||q.last_event_id.empty()||n>4096)return false;
 auto read=[&](std::vector<std::string>&v){for(std::size_t x=0;x<n;++x){std::string s;if(!(i>>std::quoted(s))||s.empty())return false;v.push_back(s);}return true;};
 if(!read(q.candidate_ids)||!(i>>n)||n>8192||!read(q.evidence_ids)||!(i>>n)||n>8192||!read(q.packet_ids))return false;
 i>>std::ws;return i.eof();
}
std::string encode_batch(const std::vector<TemporalQuestion>&v){
 std::ostringstream o;o<<"1 "<<v.size();for(const auto&q:v)o<<' '<<std::quoted(encode_one(q));return o.str();
}
bool decode_batch(const std::string&p,const std::string&eid,std::vector<TemporalQuestion>&v){
 std::istringstream i(p);int ver=0;std::size_t n=0;if(!(i>>ver>>n)||ver!=1||n==0||n>3)return false;
 for(std::size_t x=0;x<n;++x){std::string item;TemporalQuestion q;
  if(!(i>>std::quoted(item))||!decode_one(item,q)||q.last_event_id!=eid)return false;\n  v.push_back(std::move(q));\n }
 i>>std::ws;return i.eof();
}
void add_unique(std::vector<std::string>&v,const std::string&s){if(std::find(v.begin(),v.end(),s)==v.end())v.push_back(s);}
bool conflict_for(const TemporalBoundaryPending&p,const std::string&r){
 std::set<std::pair<std::string,std::string>> values;
 for(const auto&c:p.candidates)if(c.role==r)values.emplace(c.valid_from,c.valid_until);
 return values.size()>1;
}
}
TemporalQuestionLedger::TemporalQuestionLedger(EventJournal&j,std::size_t m):journal_(j),max_questions_(m){}
bool TemporalQuestionLedger::replay(){
 questions_.clear();last_error_.clear();if(!max_questions_){last_error_="TEMPORAL_QUESTION_CAPACITY_INVALID";return false;}
 for(const auto&e:journal_.committed_events())if(e.kind==kKind&&!apply_event(e)){
  last_error_="TEMPORAL_QUESTION_EVENT_INVALID:"+e.event_id;questions_.clear();return false;}
 return true;
}
bool TemporalQuestionLedger::apply_event(const JournalEvent&e){
 std::vector<TemporalQuestion> v;if(!decode_batch(e.payload,e.event_id,v))return false;
 std::size_t extra=0;for(const auto&q:v)if(!questions_.count(q.question_id))++extra;
 if(questions_.size()+extra>max_questions_)return false;
 for(auto&q:v) questions_[q.question_id]=std::move(q);\n return true;
}
TemporalQuestionResult TemporalQuestionLedger::synchronize(const std::string&tx,const std::string&ik,
 const std::string&eid,std::uint64_t version,const TemporalBoundaryPending&p){
 TemporalQuestionResult r;r.state_version=journal_.state_version();
 if(tx.empty()||ik.empty()||eid.empty()||p.claim_id.empty()||p.candidates.empty()){
  r.status=r.error="TEMPORAL_QUESTION_SYNC_ARGUMENT_INVALID";return r;}
 std::set<std::string> roles;for(const auto&c:p.candidates)roles.insert(c.role);
 std::vector<TemporalQuestion> snapshots;
 for(const auto&role:roles){
  const auto id=qid(p.claim_id,role);auto old=questions_.find(id);
  const bool conflict=conflict_for(p,role),selected=p.selected_candidate_ids.count(role)!=0;
  if(!conflict&&!(selected&&old!=questions_.end()))continue;
  TemporalQuestion q;q.question_id=id;q.claim_id=p.claim_id;q.role=role;q.last_event_id=eid;
  q.status=conflict?(old==questions_.end()?"OPEN":
   (old->second.status=="RESOLVED_PENDING_APPROVAL"?"REOPENED":"UNDER_REVIEW")):"RESOLVED_PENDING_APPROVAL";
  for(const auto&c:p.candidates)if(c.role==role){q.candidate_ids.push_back(c.candidate_id);
   add_unique(q.evidence_ids,c.evidence_id);add_unique(q.packet_ids,c.packet_id);}
  snapshots.push_back(std::move(q));
 }
 if(snapshots.empty()){r.status="NO_TEMPORAL_CONFLICT_QUESTION";return r;}
 JournalEvent event{eid,kKind,encode_batch(snapshots)};
 auto cm=journal_.commit(tx,ik,version,{event});
 r.committed=cm.committed;r.duplicate=cm.duplicate;r.state_version=cm.state_version;r.error=cm.error;
 if(!cm.committed){r.status=cm.error.empty()?"TEMPORAL_QUESTION_COMMIT_REJECTED":cm.error;return r;}
 if(!cm.duplicate&&!apply_event(event)){r.committed=false;r.status=r.error="TEMPORAL_QUESTION_REPLAY_REQUIRED";return r;}
 r.question=questions_.at(snapshots.back().question_id);r.status=r.question.status;return r;
}
bool TemporalQuestionLedger::get(const std::string&c,const std::string&r,TemporalQuestion&o)const{
 auto f=questions_.find(qid(c,r));if(f==questions_.end())return false;o=f->second;return true;}
std::vector<TemporalQuestion> TemporalQuestionLedger::for_claim(const std::string&c)const{
 std::vector<TemporalQuestion>v;for(const auto&x:questions_)if(x.second.claim_id==c)v.push_back(x.second);return v;}
std::size_t TemporalQuestionLedger::size()const{return questions_.size();}
std::string TemporalQuestionLedger::last_error()const{return last_error_;}
}
