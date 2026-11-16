#include "surge/temporal_boundary_ledger.hpp"
#include <iomanip>
#include <sstream>
namespace surge { namespace {
constexpr char K_ADD[]="TEMPORAL_BOUNDARY_CONTRIBUTION_V1";
constexpr char K_CLEAR[]="TEMPORAL_BOUNDARY_CLEARED_V1";
constexpr char K_SELECT[]="TEMPORAL_BOUNDARY_CANDIDATE_SELECTED_V1";
std::string enc_add(const TemporalBoundaryContribution& v){std::ostringstream o;o<<"1 "<<std::quoted(v.claim_id)<<' '<<std::quoted(v.role)<<' '<<std::quoted(v.valid_from)<<' '<<std::quoted(v.valid_until)<<' '<<std::quoted(v.evidence_id)<<' '<<std::quoted(v.packet_id);return o.str();}
bool dec_add(const std::string&p,TemporalBoundaryContribution&v){std::istringstream i(p);int n=0;if(!(i>>n>>std::quoted(v.claim_id)>>std::quoted(v.role)>>std::quoted(v.valid_from)>>std::quoted(v.valid_until)>>std::quoted(v.evidence_id)>>std::quoted(v.packet_id))||n!=1)return false;i>>std::ws;return i.eof();}
std::string enc_clear(const std::string&c){std::ostringstream o;o<<"1 "<<std::quoted(c);return o.str();}
bool dec_clear(const std::string&p,std::string&c){std::istringstream i(p);int n=0;if(!(i>>n>>std::quoted(c))||n!=1||c.empty())return false;i>>std::ws;return i.eof();}
std::string enc_select(const std::string&c,const std::string&r,const std::string&id){std::ostringstream o;o<<"1 "<<std::quoted(c)<<' '<<std::quoted(r)<<' '<<std::quoted(id);return o.str();}
bool dec_select(const std::string&p,std::string&c,std::string&r,std::string&id){std::istringstream i(p);int n=0;if(!(i>>n>>std::quoted(c)>>std::quoted(r)>>std::quoted(id))||n!=1||c.empty()||r.empty()||id.empty())return false;i>>std::ws;return i.eof();}
void unique_add(std::vector<std::string>&v,const std::string&x){for(const auto&y:v)if(y==x)return;v.push_back(x);}
}
TemporalBoundaryLedger::TemporalBoundaryLedger(EventJournal&j,std::size_t m):journal_(j),max_claims_(m){}
bool TemporalBoundaryLedger::replay(){pending_.clear();last_error_.clear();if(!max_claims_){last_error_="TEMPORAL_LEDGER_CAPACITY_INVALID";return false;}for(const auto&e:journal_.committed_events())if(e.kind==K_ADD||e.kind==K_CLEAR||e.kind==K_SELECT)if(!apply_event(e)){last_error_="TEMPORAL_LEDGER_EVENT_INVALID:"+e.event_id;pending_.clear();return false;}return true;}
bool TemporalBoundaryLedger::validate_contribution(const TemporalBoundaryContribution&c,std::string&s)const{
 if(c.claim_id.empty()){s="CLAIM_ID_REQUIRED";return false;}if(c.evidence_id.empty()||c.packet_id.empty()){s="PROVENANCE_REQUIRED";return false;}
 if(c.role!="VALID_FROM"&&c.role!="VALID_UNTIL"&&c.role!="INTERVAL"){s="UNSUPPORTED_TEMPORAL_ROLE";return false;}
 if(c.role=="VALID_FROM"&&(c.valid_from.empty()||!c.valid_until.empty())){s="CANDIDATE_ROLE_BOUNDARY_MISMATCH";return false;}
 if(c.role=="VALID_UNTIL"&&(!c.valid_from.empty()||c.valid_until.empty())){s="CANDIDATE_ROLE_BOUNDARY_MISMATCH";return false;}
 if(c.role=="INTERVAL"&&(c.valid_from.empty()||c.valid_until.empty())){s="CANDIDATE_INTERVAL_INCOMPLETE";return false;}
 if(pending_.find(c.claim_id)==pending_.end()&&pending_.size()>=max_claims_){s="ACCUMULATOR_CAPACITY_EXCEEDED";return false;}s="ACCEPTED";return true;
}
void TemporalBoundaryLedger::refresh_pending(TemporalBoundaryPending&p)const{
 p.valid_from.clear();p.valid_until.clear();std::map<std::string,std::vector<const TemporalBoundaryCandidate*>> g;
 for(const auto&c:p.candidates)g[c.role].push_back(&c);bool conflict=false;
 auto choose=[&](const std::string&r)->const TemporalBoundaryCandidate*{auto it=g.find(r);if(it==g.end()||it->second.empty())return nullptr;
  auto sel=p.selected_candidate_ids.find(r);if(sel!=p.selected_candidate_ids.end()){for(auto*c:it->second)if(c->candidate_id==sel->second)return c;conflict=true;return nullptr;}
  const auto&f=*it->second.front();for(auto*c:it->second)if(c->valid_from!=f.valid_from||c->valid_until!=f.valid_until)conflict=true;
  return conflict&&it->second.size()>1?nullptr:&f;};
 const auto*iv=choose("INTERVAL");const auto*vf=choose("VALID_FROM");const auto*vu=choose("VALID_UNTIL");
 if(iv){p.valid_from=iv->valid_from;p.valid_until=iv->valid_until;}else{if(vf)p.valid_from=vf->valid_from;if(vu)p.valid_until=vu->valid_until;}
 if(!p.valid_from.empty()&&!p.valid_until.empty()&&p.valid_from>=p.valid_until)conflict=true;
 for(auto&c:p.candidates){auto s=p.selected_candidate_ids.find(c.role);c.selected=s!=p.selected_candidate_ids.end()?s->second==c.candidate_id:g[c.role].size()==1;}
 p.status=conflict?"BOUNDARY_CONFLICT_REVIEW_REQUIRED":(p.valid_from.empty()||p.valid_until.empty()?"NEEDS_OTHER_BOUNDARY":"BOUNDARIES_COMPLETE_PENDING_REVIEW");
 if(!p.selected_candidate_ids.empty()&&!conflict)p.status="CONFLICT_RESOLVED_PENDING_APPROVAL";
}
bool TemporalBoundaryLedger::apply_event(const JournalEvent&e){
 if(e.kind==K_CLEAR){std::string c;if(!dec_clear(e.payload,c))return false;pending_.erase(c);return true;}
 if(e.kind==K_SELECT){std::string c,r,id;if(!dec_select(e.payload,c,r,id))return false;auto f=pending_.find(c);if(f==pending_.end())return false;bool ok=false;for(const auto&x:f->second.candidates)if(x.candidate_id==id&&x.role==r)ok=true;if(!ok)return false;f->second.selected_candidate_ids[r]=id;refresh_pending(f->second);return true;}
 TemporalBoundaryContribution c;if(!dec_add(e.payload,c))return false;std::string s;if(!validate_contribution(c,s))return false;
 auto ins=pending_.emplace(c.claim_id,TemporalBoundaryPending{});auto&p=ins.first->second;p.claim_id=c.claim_id;
 for(const auto&x:p.candidates)if(x.candidate_id==e.event_id)return x.role==c.role&&x.valid_from==c.valid_from&&x.valid_until==c.valid_until&&x.evidence_id==c.evidence_id&&x.packet_id==c.packet_id;
 p.candidates.push_back({e.event_id,c.role,c.valid_from,c.valid_until,c.evidence_id,c.packet_id,false});unique_add(p.evidence_ids,c.evidence_id);unique_add(p.packet_ids,c.packet_id);refresh_pending(p);return true;
}
TemporalBoundaryLedgerResult TemporalBoundaryLedger::result_for(const std::string&id,const JournalCommitResult&c)const{TemporalBoundaryLedgerResult r;r.committed=c.committed;r.duplicate=c.duplicate;r.state_version=c.state_version;r.error=c.error;auto f=pending_.find(id);if(f!=pending_.end()){r.pending=f->second;r.status=f->second.status;}else{r.pending.claim_id=id;r.status="CLEARED";}return r;}
TemporalBoundaryLedgerResult TemporalBoundaryLedger::add(const std::string&tx,const std::string&ik,const std::string&eid,std::uint64_t v,const TemporalBoundaryContribution&c){std::string s;if(!validate_contribution(c,s)){TemporalBoundaryLedgerResult r;r.state_version=journal_.state_version();r.status=s;r.error=s;return r;}JournalEvent e{eid,K_ADD,enc_add(c)};auto cm=journal_.commit(tx,ik,v,{e});if(cm.committed&&!cm.duplicate&&!apply_event(e)){TemporalBoundaryLedgerResult r;r.state_version=cm.state_version;r.status="COMMITTED_REPLAY_REQUIRED";r.error=r.status;return r;}return result_for(c.claim_id,cm);}
TemporalBoundaryLedgerResult TemporalBoundaryLedger::resolve_candidate(const std::string&tx,const std::string&ik,const std::string&eid,std::uint64_t v,const std::string&claim,const std::string&role,const std::string&id){auto f=pending_.find(claim);if(f==pending_.end()){TemporalBoundaryLedgerResult r;r.state_version=journal_.state_version();r.status="CLAIM_NOT_FOUND";r.error=r.status;return r;}bool ok=false;for(const auto&c:f->second.candidates)if(c.candidate_id==id&&c.role==role)ok=true;if(!ok){TemporalBoundaryLedgerResult r;r.state_version=journal_.state_version();r.status="CANDIDATE_NOT_FOUND";r.error=r.status;return r;}JournalEvent e{eid,K_SELECT,enc_select(claim,role,id)};auto cm=journal_.commit(tx,ik,v,{e});if(cm.committed&&!cm.duplicate&&!apply_event(e)){TemporalBoundaryLedgerResult r;r.state_version=cm.state_version;r.status="COMMITTED_REPLAY_REQUIRED";r.error=r.status;return r;}return result_for(claim,cm);}
TemporalBoundaryLedgerResult TemporalBoundaryLedger::clear(const std::string&tx,const std::string&ik,const std::string&eid,std::uint64_t v,const std::string&claim){if(claim.empty()){TemporalBoundaryLedgerResult r;r.state_version=journal_.state_version();r.status="CLAIM_ID_REQUIRED";r.error=r.status;return r;}JournalEvent e{eid,K_CLEAR,enc_clear(claim)};auto cm=journal_.commit(tx,ik,v,{e});if(cm.committed&&!cm.duplicate)pending_.erase(claim);return result_for(claim,cm);}
bool TemporalBoundaryLedger::get(const std::string&id,TemporalBoundaryPending&o)const{auto f=pending_.find(id);if(f==pending_.end())return false;o=f->second;return true;}
std::size_t TemporalBoundaryLedger::pending_claim_count()const{return pending_.size();}
std::string TemporalBoundaryLedger::last_error()const{return last_error_;}
} // namespace surge
