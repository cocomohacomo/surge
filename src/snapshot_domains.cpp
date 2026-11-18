#include "surge/snapshot_domains.hpp"
#include <iomanip>
#include <mutex>
#include <sstream>
#include <utility>
namespace surge { namespace {
bool fail(std::string* e,const char* m){if(e)*e=m;return false;}
constexpr std::size_t kMaxIds=100000,kMaxRevisions=1000000;
std::string encode_history(const std::map<std::string,std::vector<EvidenceProvenanceRecord>>& h){
 std::ostringstream o;o<<"EPH1 "<<h.size()<<' ';
 for(const auto& x:h){o<<std::quoted(x.first)<<' '<<x.second.size()<<' ';
  for(const auto& r:x.second){const auto&p=r.provenance;o<<std::quoted(p.evidence_id)<<' '<<p.revision<<' '<<r.recorded_state_version<<' ';
   auto f=[&](const std::optional<std::string>&v){o<<(v?1:0)<<' ';if(v)o<<std::quoted(*v)<<' ';};
   f(p.observed_at);f(p.source_published_at);f(p.source_retrieved_at);
  }
 }return o.str();
}
bool decode_history(const std::string& payload,std::map<std::string,std::vector<EvidenceProvenanceRecord>>& output){
 std::istringstream in(payload);std::string magic,extra;std::size_t ids=0,total=0;
 if(!(in>>magic>>ids)||magic!="EPH1"||ids>kMaxIds)return false;
 std::map<std::string,std::vector<EvidenceProvenanceRecord>> candidate;
 for(std::size_t i=0;i<ids;++i){std::string key;std::size_t n=0;
  if(!(in>>std::quoted(key)>>n)||key.empty()||n>kMaxRevisions||n>kMaxRevisions-total||candidate.count(key))return false;
  total+=n;std::vector<EvidenceProvenanceRecord> records;records.reserve(n);std::uint64_t previous=0;
  for(std::size_t j=0;j<n;++j){EvidenceProvenanceRecord r;auto&p=r.provenance;
   if(!(in>>std::quoted(p.evidence_id)>>p.revision>>r.recorded_state_version)||p.evidence_id!=key||p.revision!=j+1||!r.recorded_state_version||r.recorded_state_version<=previous)return false;
   auto f=[&](std::optional<std::string>&v){int present=0;if(!(in>>present)||(present!=0&&present!=1))return false;if(!present){v.reset();return true;}std::string s;if(!(in>>std::quoted(s))||!is_valid_rfc3339_timestamp(s))return false;v=std::move(s);return true;};
   if(!f(p.observed_at)||!f(p.source_published_at)||!f(p.source_retrieved_at))return false;
   previous=r.recorded_state_version;records.push_back(std::move(r));
  }candidate.emplace(std::move(key),std::move(records));
 }
 if(in>>extra)return false;output.swap(candidate);return true;
}
}  // namespace
class PreparedEvidenceSnapshotRestore final:public PreparedSnapshotRestore{
 public:PreparedEvidence(EvidenceProvenanceLedger*o,std::map<std::string,std::vector<EvidenceProvenanceRecord>>c):owner(o),candidate(std::move(c)){}
 void commit()noexcept override{owner->history_.swap(candidate);}
 private:EvidenceProvenanceLedger*owner;std::map<std::string,std::vector<EvidenceProvenanceRecord>>candidate;
};
class PreparedChaosSnapshotRestore final:public PreparedSnapshotRestore{
 public:PreparedChaos(ChaosClusterLedger*o,ChaosClusterLedger::State c):owner(o),candidate(std::move(c)){}
 void commit()noexcept override{try{std::lock_guard<std::mutex> lock(owner->mutex_);owner->state_.swap(candidate);}catch(...){std::terminate();}}
 private:ChaosClusterLedger*owner;ChaosClusterLedger::State candidate;
};
std::string EvidenceProvenanceSnapshotDomain::domain_id()const{return "evidence_provenance";}
std::string EvidenceProvenanceSnapshotDomain::schema_id()const{return "surge.evidence-provenance.v1";}
bool EvidenceProvenanceSnapshotDomain::capture(std::string*p,std::string*e)const{if(!p)return fail(e,"SNAPSHOT_OUTPUT_REQUIRED");*p=encode_history(ledger_.history_);if(e)e->clear();return true;}
std::unique_ptr<PreparedSnapshotRestore> EvidenceProvenanceSnapshotDomain::prepare_restore(const std::string&p,std::string*e){
 std::map<std::string,std::vector<EvidenceProvenanceRecord>> c;if(!decode_history(p,c)){fail(e,"SNAPSHOT_EVIDENCE_PROVENANCE_INVALID");return nullptr;}
 if(e)e->clear();return std::make_unique<PreparedEvidenceSnapshotRestore>(&ledger_,std::move(c));
}
std::string ChaosClusterSnapshotDomain::domain_id()const{return "chaos_cluster";}
std::string ChaosClusterSnapshotDomain::schema_id()const{return "surge.chaos-cluster.native-v1";}
bool ChaosClusterSnapshotDomain::capture(std::string*p,std::string*e)const{
 if(!p)return fail(e,"SNAPSHOT_OUTPUT_REQUIRED");std::lock_guard<std::mutex>lock(ledger_.mutex_);*p=ChaosClusterLedger::encode(ledger_.state_);if(e)e->clear();return true;
}
std::unique_ptr<PreparedSnapshotRestore> ChaosClusterSnapshotDomain::prepare_restore(const std::string&p,std::string*e){
 ChaosClusterLedger::State c;if(!ChaosClusterLedger::decode(p,c)){fail(e,"SNAPSHOT_CHAOS_CLUSTER_INVALID");return nullptr;}
 for(const auto&x:c)if(x.first.empty()||x.first!=x.second.cluster_id){fail(e,"SNAPSHOT_CHAOS_CLUSTER_ID_MISMATCH");return nullptr;}
 if(e)e->clear();return std::make_unique<PreparedChaosSnapshotRestore>(&ledger_,std::move(c));
}
} // namespace surge
