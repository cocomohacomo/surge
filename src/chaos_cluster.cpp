#include "surge/chaos_cluster.hpp"
#include <iomanip>
#include <sstream>
#include <utility>
#include <set>
#include <cmath>

namespace surge { namespace {
const char* ln(ChaosLifecycle v){switch(v){case ChaosLifecycle::Open:return "OPEN";case ChaosLifecycle::Accumulating:return "ACCUMULATING";case ChaosLifecycle::Emerging:return "EMERGING";case ChaosLifecycle::Reviewable:return "REVIEWABLE";case ChaosLifecycle::Promoted:return "PROMOTED";case ChaosLifecycle::Forked:return "FORKED";case ChaosLifecycle::Dormant:return "DORMANT";case ChaosLifecycle::Archived:return "ARCHIVED";}return "";}
bool pl(const std::string&s,ChaosLifecycle&v){const std::pair<const char*,ChaosLifecycle>a[]={{"OPEN",ChaosLifecycle::Open},{"ACCUMULATING",ChaosLifecycle::Accumulating},{"EMERGING",ChaosLifecycle::Emerging},{"REVIEWABLE",ChaosLifecycle::Reviewable},{"PROMOTED",ChaosLifecycle::Promoted},{"FORKED",ChaosLifecycle::Forked},{"DORMANT",ChaosLifecycle::Dormant},{"ARCHIVED",ChaosLifecycle::Archived}};for(const auto&x:a)if(s==x.first){v=x.second;return true;}return false;}
const char* on(ChaosOrganization v){switch(v){case ChaosOrganization::Unordered:return "UNORDERED";case ChaosOrganization::Provisional:return "PROVISIONAL";case ChaosOrganization::MultiPattern:return "MULTI_PATTERN";case ChaosOrganization::PartiallyStructured:return "PARTIALLY_STRUCTURED";}return "";}
bool po(const std::string&s,ChaosOrganization&v){const std::pair<const char*,ChaosOrganization>a[]={{"UNORDERED",ChaosOrganization::Unordered},{"PROVISIONAL",ChaosOrganization::Provisional},{"MULTI_PATTERN",ChaosOrganization::MultiPattern},{"PARTIALLY_STRUCTURED",ChaosOrganization::PartiallyStructured}};for(const auto&x:a)if(s==x.first){v=x.second;return true;}return false;}
const char* tn(ChaosAssociationType v){switch(v){case ChaosAssociationType::CoOccurs:return "CO_OCCURS";case ChaosAssociationType::Analogy:return "ANALOGY";case ChaosAssociationType::Hypothesis:return "HYPOTHESIS";case ChaosAssociationType::Question:return "QUESTION";case ChaosAssociationType::Contradiction:return "CONTRADICTION";case ChaosAssociationType::TemporalNearness:return "TEMPORAL_NEARNESS";case ChaosAssociationType::Unclassified:return "UNCLASSIFIED";case ChaosAssociationType::CreativeSynthesis:return "CREATIVE_SYNTHESIS";}return "";}
bool pt(const std::string&s,ChaosAssociationType&v){const std::pair<const char*,ChaosAssociationType>a[]={{"CO_OCCURS",ChaosAssociationType::CoOccurs},{"ANALOGY",ChaosAssociationType::Analogy},{"HYPOTHESIS",ChaosAssociationType::Hypothesis},{"QUESTION",ChaosAssociationType::Question},{"CONTRADICTION",ChaosAssociationType::Contradiction},{"TEMPORAL_NEARNESS",ChaosAssociationType::TemporalNearness},{"UNCLASSIFIED",ChaosAssociationType::Unclassified},{"CREATIVE_SYNTHESIS",ChaosAssociationType::CreativeSynthesis}};for(const auto&x:a)if(s==x.first){v=x.second;return true;}return false;}
const char* sn(ChaosAssociationStatus v){switch(v){case ChaosAssociationStatus::Provisional:return "PROVISIONAL";case ChaosAssociationStatus::Reviewed:return "REVIEWED";case ChaosAssociationStatus::Rejected:return "REJECTED";case ChaosAssociationStatus::Expired:return "EXPIRED";}return "";}
bool ps(const std::string&s,ChaosAssociationStatus&v){const std::pair<const char*,ChaosAssociationStatus>a[]={{"PROVISIONAL",ChaosAssociationStatus::Provisional},{"REVIEWED",ChaosAssociationStatus::Reviewed},{"REJECTED",ChaosAssociationStatus::Rejected},{"EXPIRED",ChaosAssociationStatus::Expired}};for(const auto&x:a)if(s==x.first){v=x.second;return true;}return false;}
void wp(std::ostream&o,const ChaosProvenance&p){o<<std::quoted(p.producer_id)<<' '<<std::quoted(p.producer_version)<<' '<<std::quoted(p.policy_version)<<' '<<std::quoted(p.recorded_at)<<' ';}
bool rp(std::istream&i,ChaosProvenance&p){return bool(i>>std::quoted(p.producer_id)>>std::quoted(p.producer_version)>>std::quoted(p.policy_version)>>std::quoted(p.recorded_at));}
bool vp(const ChaosProvenance&p){return !p.producer_id.empty()&&!p.producer_version.empty()&&!p.policy_version.empty()&&!p.recorded_at.empty();}
bool utc_timestamp(const std::string&v){
 if(v.empty())return true;
 if(v.size()!=20||v[4]!='-'||v[7]!='-'||v[10]!='T'||v[13]!=':'||v[16]!=':'||v[19]!='Z')return false;
 for(std::size_t i=0;i<v.size();++i)if(i!=4&&i!=7&&i!=10&&i!=13&&i!=16&&i!=19&&
    (v[i]<'0'||v[i]>'9'))return false;
 return true;
}
bool same_provenance(const ChaosProvenance&a,const ChaosProvenance&b){return a.producer_id==b.producer_id&&a.producer_version==b.producer_version&&a.policy_version==b.policy_version&&a.recorded_at==b.recorded_at;}
bool same_member(const ChaosMember&a,const ChaosMember&b){return a.member_ref==b.member_ref&&a.event_ref==b.event_ref&&a.admission_reason==b.admission_reason&&a.admitted_at==b.admitted_at&&a.source_digest==b.source_digest&&same_provenance(a.provenance,b.provenance);}
void write_member(std::ostream&o,const ChaosMember&m){o<<std::quoted(m.member_ref)<<' '<<std::quoted(m.event_ref)<<' '<<std::quoted(m.admission_reason)<<' '<<std::quoted(m.admitted_at)<<' '<<std::quoted(m.source_digest)<<' ';wp(o,m.provenance);}
bool read_member(std::istream&i,ChaosMember&m){return bool(i>>std::quoted(m.member_ref)>>std::quoted(m.event_ref)>>std::quoted(m.admission_reason)>>std::quoted(m.admitted_at)>>std::quoted(m.source_digest))&&rp(i,m.provenance)&&!m.member_ref.empty()&&!m.event_ref.empty();}
}
ChaosClusterLedger::ChaosClusterLedger(std::string p):owned_journal_(std::make_unique<EventJournal>(std::move(p))),journal_(owned_journal_.get()){}
ChaosClusterLedger::ChaosClusterLedger(EventJournal&j):journal_(&j){}
bool ChaosClusterLedger::allowed_transition(ChaosLifecycle a,ChaosLifecycle b){if(a==b)return true;switch(a){case ChaosLifecycle::Open:return b==ChaosLifecycle::Accumulating;case ChaosLifecycle::Accumulating:return b==ChaosLifecycle::Emerging||b==ChaosLifecycle::Reviewable||b==ChaosLifecycle::Dormant;case ChaosLifecycle::Emerging:return b==ChaosLifecycle::Accumulating||b==ChaosLifecycle::Reviewable||b==ChaosLifecycle::Dormant;case ChaosLifecycle::Reviewable:return b==ChaosLifecycle::Accumulating||b==ChaosLifecycle::Promoted||b==ChaosLifecycle::Forked||b==ChaosLifecycle::Dormant;case ChaosLifecycle::Promoted:return b==ChaosLifecycle::Accumulating||b==ChaosLifecycle::Dormant||b==ChaosLifecycle::Archived;case ChaosLifecycle::Forked:return b==ChaosLifecycle::Accumulating||b==ChaosLifecycle::Dormant||b==ChaosLifecycle::Archived;case ChaosLifecycle::Dormant:return b==ChaosLifecycle::Accumulating||b==ChaosLifecycle::Emerging||b==ChaosLifecycle::Reviewable||b==ChaosLifecycle::Archived;case ChaosLifecycle::Archived:return b==ChaosLifecycle::Dormant;}return false;}
std::string ChaosClusterLedger::encode(const State&s){
 std::ostringstream o;o<<s.size()<<' ';
 for(const auto&kv:s){const auto&c=kv.second;o<<std::quoted(c.cluster_id)<<' '<<ln(c.lifecycle)<<' '<<c.state_version<<' '<<c.members.size()<<' ';
  for(const auto&x:c.members)write_member(o,x.second);
  o<<c.associations.size()<<' ';for(const auto&x:c.associations){const auto&a=x.second;o<<std::quoted(a.association_id)<<' '<<std::quoted(a.from_ref)<<' '<<std::quoted(a.to_ref)<<' '<<tn(a.type)<<' '<<sn(a.status)<<' ';wp(o,a.provenance);}
  o<<c.parent_refs.size()<<' ';for(const auto&x:c.parent_refs)o<<std::quoted(x)<<' ';
  o<<c.child_refs.size()<<' ';for(const auto&x:c.child_refs)o<<std::quoted(x)<<' ';
  o<<c.promoted_member_targets.size()<<' ';for(const auto&x:c.promoted_member_targets)o<<std::quoted(x.first)<<' '<<std::quoted(x.second)<<' ';
 }
 // Optional extension follows the v0.1-compatible base payload. Old snapshots remain readable.
 o<<"CC2 "<<s.size()<<' ';
 for(const auto&kv:s){const auto&c=kv.second;o<<std::quoted(c.cluster_id)<<' '<<on(c.organization)<<' ';wp(o,c.creation_provenance);
  o<<c.associations.size()<<' ';for(const auto&x:c.associations){const auto&a=x.second;o<<std::quoted(x.first)<<' '<<a.history.size()<<' ';for(const auto&h:a.history){o<<sn(h.status)<<' ';wp(o,h.provenance);}}
  o<<c.pattern_hypotheses.size()<<' ';for(const auto&x:c.pattern_hypotheses){const auto&h=x.second;o<<std::quoted(h.hypothesis_id)<<' '<<std::quoted(h.statement)<<' '<<std::quoted(h.epistemic_state)<<' '<<std::quoted(h.status)<<' ';auto vec=[&](const std::vector<std::string>&v){o<<v.size()<<' ';for(const auto&z:v)o<<std::quoted(z)<<' ';};vec(h.member_refs);vec(h.excluded_member_refs);vec(h.counterexample_refs);vec(h.alternative_hypothesis_refs);wp(o,h.provenance);}
 }
 // CC3 adds append-only, scorer-bound observations without changing CC2 snapshots.
 o<<"CC3 "<<s.size()<<' ';
 for(const auto&kv:s){const auto&c=kv.second;o<<std::quoted(c.cluster_id)<<' '<<c.observations.size()<<' ';
  for(const auto&dimension:c.observations){o<<std::quoted(dimension.first)<<' '<<dimension.second.size()<<' ';
   for(const auto&v:dimension.second){o<<std::setprecision(17)<<v.value<<' '<<std::quoted(v.scorer_id)<<' '
    <<std::quoted(v.scorer_version)<<' '<<std::quoted(v.policy_version)<<' '<<std::quoted(v.observed_at)<<' ';}
  }
 }
 // CC4 persists append-only membership state and explicit unmembership revisions.
 o<<"CC4 "<<s.size()<<' ';
 for(const auto&kv:s){const auto&c=kv.second;o<<std::quoted(c.cluster_id)<<' '<<c.members.size()<<' ';
  for(const auto&member:c.members){const auto&m=member.second;
   o<<std::quoted(member.first)<<' '<<std::quoted(m.membership_status)<<' '
    <<(m.membership_active?1:0)<<' '<<m.membership_history.size()<<' ';
   for(const auto&revision:m.membership_history){
    o<<std::quoted(revision.status)<<' '<<(revision.active?1:0)<<' ';wp(o,revision.provenance);
   }
  }
 }
 // CC5 persists entity-scoped observations independently of cluster-level scores.
 auto write_observations=[&](const std::vector<ChaosScoreObservation>&history){
  o<<history.size()<<' ';
  for(const auto&v:history)o<<std::setprecision(17)<<v.value<<' '<<std::quoted(v.scorer_id)<<' '
   <<std::quoted(v.scorer_version)<<' '<<std::quoted(v.policy_version)<<' '<<std::quoted(v.observed_at)<<' ';
 };
 o<<"CC5 "<<s.size()<<' ';
 for(const auto&kv:s){const auto&c=kv.second;o<<std::quoted(c.cluster_id)<<' '<<c.members.size()<<' ';
  for(const auto&item:c.members){o<<std::quoted(item.first)<<' ';write_observations(item.second.salience_observations);}
  o<<c.associations.size()<<' ';
  for(const auto&item:c.associations){o<<std::quoted(item.first)<<' ';write_observations(item.second.confidence_observations);}
  o<<c.pattern_hypotheses.size()<<' ';
  for(const auto&item:c.pattern_hypotheses){o<<std::quoted(item.first)<<' ';write_observations(item.second.confidence_observations);}
 }
 // CC6 persists association valid-time bounds independently of status revisions.
 o<<"CC6 "<<s.size()<<' ';
 for(const auto&kv:s){const auto&c=kv.second;o<<std::quoted(c.cluster_id)<<' '<<c.associations.size()<<' ';
  for(const auto&item:c.associations)o<<std::quoted(item.first)<<' '<<std::quoted(item.second.valid_from)<<' '<<std::quoted(item.second.valid_until)<<' ';
 }
 return o.str();
}
bool ChaosClusterLedger::decode(const std::string&p,State&s){
 std::istringstream i(p);std::size_t n=0;if(!(i>>n)||n>100000)return false;
 for(std::size_t z=0;z<n;++z){ChaosCluster c;std::string life;std::size_t nm=0,na=0,np=0,nc=0,npr=0;
  if(!(i>>std::quoted(c.cluster_id)>>life>>c.state_version)||c.cluster_id.empty()||!pl(life,c.lifecycle)||!s.emplace(c.cluster_id,c).second)return false;
  auto&v=s.at(c.cluster_id);if(!(i>>nm)||nm>1000000)return false;
  for(std::size_t x=0;x<nm;++x){ChaosMember m;if(!read_member(i,m)||!v.members.emplace(m.member_ref,m).second)return false;}
  if(!(i>>na)||na>1000000)return false;
  for(std::size_t x=0;x<na;++x){ChaosAssociation a;std::string t,st;if(!(i>>std::quoted(a.association_id)>>std::quoted(a.from_ref)>>std::quoted(a.to_ref)>>t>>st)||!pt(t,a.type)||!ps(st,a.status)||!rp(i,a.provenance)||a.association_id.empty()||!v.associations.emplace(a.association_id,a).second)return false;}
  if(!(i>>np)||np>100000)return false;
  for(std::size_t x=0;x<np;++x){std::string a;if(!(i>>std::quoted(a)))return false;v.parent_refs.push_back(a);}
  if(!(i>>nc)||nc>100000)return false;
  for(std::size_t x=0;x<nc;++x){std::string a;if(!(i>>std::quoted(a)))return false;v.child_refs.push_back(a);}
  if(!(i>>npr)||npr>1000000)return false;
  for(std::size_t x=0;x<npr;++x){std::string a,b;if(!(i>>std::quoted(a)>>std::quoted(b))||!v.promoted_member_targets.emplace(a,b).second)return false;}
 }
 std::string marker;if(!(i>>marker))return true;
 if(marker!="CC2")return false;
 std::size_t ext=0;if(!(i>>ext)||ext!=s.size())return false;
 for(std::size_t z=0;z<ext;++z){std::string id,org;ChaosProvenance cp;std::size_t na=0,nh=0;
  if(!(i>>std::quoted(id)>>org)||!s.count(id)||!po(org,s.at(id).organization)||!rp(i,cp))return false;
  auto&c=s.at(id);c.creation_provenance=std::move(cp);
  if(!(i>>na)||na!=c.associations.size())return false;
  for(std::size_t x=0;x<na;++x){std::string aid;if(!(i>>std::quoted(aid)>>nh)||!c.associations.count(aid)||nh>100000)return false;auto&a=c.associations.at(aid);for(std::size_t y=0;y<nh;++y){ChaosStatusRevision h;std::string st;if(!(i>>st)||!ps(st,h.status)||!rp(i,h.provenance))return false;a.history.push_back(std::move(h));}}
  if(!(i>>nh)||nh>100000)return false;
  for(std::size_t x=0;x<nh;++x){ChaosPatternHypothesis h;if(!(i>>std::quoted(h.hypothesis_id)>>std::quoted(h.statement)>>std::quoted(h.epistemic_state)>>std::quoted(h.status))||h.hypothesis_id.empty())return false;auto vec=[&](std::vector<std::string>&v){std::size_t count=0;if(!(i>>count)||count>100000)return false;for(std::size_t q=0;q<count;++q){std::string value;if(!(i>>std::quoted(value)))return false;v.push_back(std::move(value));}return true;};if(!vec(h.member_refs)||!vec(h.excluded_member_refs)||!vec(h.counterexample_refs)||!vec(h.alternative_hypothesis_refs)||!rp(i,h.provenance)||!c.pattern_hypotheses.emplace(h.hypothesis_id,std::move(h)).second)return false;}
 }
 std::string extension_marker;
 if(!(i>>extension_marker))return true;
 if(extension_marker!="CC3")return false;
 std::size_t count=0;if(!(i>>count)||count!=s.size())return false;
 for(std::size_t z=0;z<count;++z){std::string id;std::size_t dimensions=0;
  if(!(i>>std::quoted(id)>>dimensions)||!s.count(id)||dimensions>32)return false;
  auto&observations=s.at(id).observations;
  for(std::size_t d=0;d<dimensions;++d){std::string dimension;std::size_t values=0;
   if(!(i>>std::quoted(dimension)>>values)||values>100000||
      (dimension!="organization"&&dimension!="salience"&&dimension!="recognition_confidence"&&
       dimension!="novelty"&&dimension!="diversity"&&dimension!="ambiguity"&&dimension!="utility"))return false;
   auto&history=observations[dimension];
   for(std::size_t x=0;x<values;++x){ChaosScoreObservation v;
    if(!(i>>v.value>>std::quoted(v.scorer_id)>>std::quoted(v.scorer_version)>>
         std::quoted(v.policy_version)>>std::quoted(v.observed_at))||
       !std::isfinite(v.value)||v.value<0.0||v.value>1.0||v.scorer_id.empty()||
       v.scorer_version.empty()||v.policy_version.empty()||v.observed_at.empty())return false;
    history.push_back(std::move(v));
   }
  }
 }
 std::string membership_marker;
 if(!(i>>membership_marker))return true;
 if(membership_marker!="CC4")return false;
 std::size_t cluster_count=0;if(!(i>>cluster_count)||cluster_count!=s.size())return false;
 for(std::size_t z=0;z<cluster_count;++z){std::string cluster_id;std::size_t member_count=0;
  if(!(i>>std::quoted(cluster_id)>>member_count)||!s.count(cluster_id)||member_count!=s.at(cluster_id).members.size())return false;
  auto&members=s.at(cluster_id).members;
  for(std::size_t x=0;x<member_count;++x){std::string member_ref,status;int active=0;std::size_t revisions=0;
   if(!(i>>std::quoted(member_ref)>>std::quoted(status)>>active>>revisions)||
      !members.count(member_ref)||(status!="UNCLASSIFIED"&&status!="PROVISIONAL"&&status!="REVIEWED")||
      (active!=0&&active!=1)||revisions>100000)return false;
   auto&m=members.at(member_ref);m.membership_status=status;m.membership_active=active==1;
   for(std::size_t y=0;y<revisions;++y){ChaosMembershipRevision revision;int revision_active=0;
    if(!(i>>std::quoted(revision.status)>>revision_active)|| 
       (revision.status!="UNCLASSIFIED"&&revision.status!="PROVISIONAL"&&revision.status!="REVIEWED")||
       (revision_active!=0&&revision_active!=1)||!rp(i,revision.provenance))return false;
    revision.active=revision_active==1;m.membership_history.push_back(std::move(revision));
   }
   if(!m.membership_history.empty()&&
      (m.membership_history.back().status!=m.membership_status||
       m.membership_history.back().active!=m.membership_active))return false;
  }
 }
 std::string entity_marker;
 if(!(i>>entity_marker))return true;
 if(entity_marker!="CC5")return false;
 std::size_t entity_clusters=0;if(!(i>>entity_clusters)||entity_clusters!=s.size())return false;
 auto read_observations=[&](std::vector<ChaosScoreObservation>&history){
  std::size_t observation_count=0;if(!(i>>observation_count)||observation_count>100000)return false;
  for(std::size_t x=0;x<observation_count;++x){ChaosScoreObservation v;
   if(!(i>>v.value>>std::quoted(v.scorer_id)>>std::quoted(v.scorer_version)>>
        std::quoted(v.policy_version)>>std::quoted(v.observed_at))||
      !std::isfinite(v.value)||v.value<0.0||v.value>1.0||v.scorer_id.empty()||
      v.scorer_version.empty()||v.policy_version.empty()||v.observed_at.empty())return false;
   history.push_back(std::move(v));
  }
  return true;
 };
 for(std::size_t z=0;z<entity_clusters;++z){std::string id;std::size_t count=0;
  if(!(i>>std::quoted(id)>>count)||!s.count(id)||count!=s.at(id).members.size())return false;
  auto&c=s.at(id);
  for(std::size_t x=0;x<count;++x){std::string ref;if(!(i>>std::quoted(ref))||!c.members.count(ref)||!read_observations(c.members.at(ref).salience_observations))return false;}
  if(!(i>>count)||count!=c.associations.size())return false;
  for(std::size_t x=0;x<count;++x){std::string ref;if(!(i>>std::quoted(ref))||!c.associations.count(ref)||!read_observations(c.associations.at(ref).confidence_observations))return false;}
  if(!(i>>count)||count!=c.pattern_hypotheses.size())return false;
  for(std::size_t x=0;x<count;++x){std::string ref;if(!(i>>std::quoted(ref))||!c.pattern_hypotheses.count(ref)||!read_observations(c.pattern_hypotheses.at(ref).confidence_observations))return false;}
 }
 std::string validity_marker;
 if(!(i>>validity_marker))return true;
 if(validity_marker!="CC6")return false;
 std::size_t validity_clusters=0;if(!(i>>validity_clusters)||validity_clusters!=s.size())return false;
 for(std::size_t z=0;z<validity_clusters;++z){std::string id;std::size_t count=0;
  if(!(i>>std::quoted(id)>>count)||!s.count(id)||count!=s.at(id).associations.size())return false;
  auto&associations=s.at(id).associations;
  for(std::size_t x=0;x<count;++x){std::string ref,from,until;
   if(!(i>>std::quoted(ref)>>std::quoted(from)>>std::quoted(until))||!associations.count(ref)||
      !utc_timestamp(from)||!utc_timestamp(until)||(!from.empty()&&!until.empty()&&from>=until))return false;
   associations.at(ref).valid_from=std::move(from);associations.at(ref).valid_until=std::move(until);
  }
 }
 std::string extra;return !(i>>extra);
}
bool ChaosClusterLedger::replay(){State staged;for(const auto&e:journal_->committed_events())if(e.kind=="CHAOS_CLUSTER_SNAPSHOT"){State next;if(!decode(e.payload,next))return false;staged=std::move(next);}state_=std::move(staged);return true;}
bool ChaosClusterLedger::open(){std::lock_guard<std::mutex>lock(mutex_);return journal_&&journal_->open()&&replay();}
ChaosResult ChaosClusterLedger::commit_snapshot(const std::string&op,const std::string&key,std::uint64_t expected,State&&s,const std::string&err){if(!err.empty())return{false,false,journal_->state_version(),err};for(auto&x:s)x.second.state_version=expected+1;JournalEvent e{op,"CHAOS_CLUSTER_SNAPSHOT",encode(s)};auto r=journal_->commit(op,key,expected,{e});if(!r.committed)return{false,r.duplicate,r.state_version,r.error};state_=std::move(s);return{true,r.duplicate,r.state_version,{}};}
ChaosResult ChaosClusterLedger::create(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const ChaosProvenance&p){std::lock_guard<std::mutex>lock(mutex_);if(id.empty()||!vp(p))return{false,false,state_version(),"INVALID_CLUSTER_OR_PROVENANCE"};State s=state_;auto old=s.find(id);if(old!=s.end())return{same_provenance(old->second.creation_provenance,p),true,state_version(),same_provenance(old->second.creation_provenance,p)?std::string():"CHAOS_CLUSTER_ID_CONFLICT"};ChaosCluster c;c.cluster_id=id;c.creation_provenance=p;s.emplace(id,std::move(c));return commit_snapshot(o,k,v,std::move(s),{});}
ChaosResult ChaosClusterLedger::admit_member(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const ChaosMember&m){std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};if(it->second.lifecycle==ChaosLifecycle::Archived)return{false,false,state_version(),"CHAOS_CLUSTER_ARCHIVED"};if(m.member_ref.empty()||m.event_ref.empty()||m.admission_reason.empty()||m.admitted_at.empty()||!vp(m.provenance))return{false,false,state_version(),"INVALID_CHAOS_MEMBER"};auto old=it->second.members.find(m.member_ref);if(old!=it->second.members.end())return{same_member(old->second,m),true,state_version(),same_member(old->second,m)?std::string():"CHAOS_MEMBER_ID_CONFLICT"};auto member=m;if(member.membership_status!="UNCLASSIFIED"&&member.membership_status!="PROVISIONAL"&&member.membership_status!="REVIEWED")return{false,false,state_version(),"CHAOS_MEMBERSHIP_STATUS_INVALID"};if(member.membership_history.empty())member.membership_history.push_back({member.membership_status,true,member.provenance});it->second.members.emplace(m.member_ref,std::move(member));if(it->second.lifecycle==ChaosLifecycle::Open)it->second.lifecycle=ChaosLifecycle::Accumulating;return commit_snapshot(o,k,v,std::move(s),{});}
ChaosResult ChaosClusterLedger::revise_membership(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const std::string&member_ref,const std::string&status,bool active,const ChaosProvenance&p){
 std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto cluster=s.find(id);
 if(cluster==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};
 auto member=cluster->second.members.find(member_ref);
 if(member==cluster->second.members.end())return{false,false,state_version(),"CHAOS_MEMBER_NOT_FOUND"};
 if((status!="UNCLASSIFIED"&&status!="PROVISIONAL"&&status!="REVIEWED")||!vp(p))
  return{false,false,state_version(),"CHAOS_MEMBERSHIP_REVISION_INVALID"};
 auto&m=member->second;
 if(m.membership_status==status&&m.membership_active==active)return{true,true,state_version(),{}};
 if(m.membership_history.size()>=100000)return{false,false,state_version(),"CHAOS_MEMBERSHIP_HISTORY_LIMIT"};
 m.membership_status=status;m.membership_active=active;
 m.membership_history.push_back({status,active,p});
 return commit_snapshot(o,k,v,std::move(s),{});
}
ChaosResult ChaosClusterLedger::propose_association(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const ChaosAssociation&a){std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};if(a.association_id.empty()||a.from_ref==a.to_ref||!vp(a.provenance)||
 !utc_timestamp(a.valid_from)||!utc_timestamp(a.valid_until)||
 (!a.valid_from.empty()&&!a.valid_until.empty()&&a.valid_from>=a.valid_until))
 return{false,false,state_version(),"INVALID_CHAOS_ASSOCIATION"};if(!it->second.members.count(a.from_ref)||!it->second.members.count(a.to_ref))return{false,false,state_version(),"CHAOS_ASSOCIATION_MEMBER_NOT_FOUND"};auto old=it->second.associations.find(a.association_id);if(old!=it->second.associations.end()){const bool same=old->second.from_ref==a.from_ref&&old->second.to_ref==a.to_ref&&old->second.type==a.type&&!old->second.history.empty()&&same_provenance(old->second.history.front().provenance,a.provenance);return{same,true,state_version(),same?std::string():"CHAOS_ASSOCIATION_ID_CONFLICT"};}auto x=a;x.status=ChaosAssociationStatus::Provisional;x.history.push_back({ChaosAssociationStatus::Provisional,a.provenance});it->second.associations.emplace(x.association_id,std::move(x));return commit_snapshot(o,k,v,std::move(s),{});}
ChaosResult ChaosClusterLedger::review_association(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const std::string&aid,ChaosAssociationStatus st,const ChaosProvenance&p){std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};auto a=it->second.associations.find(aid);if(a==it->second.associations.end())return{false,false,state_version(),"CHAOS_ASSOCIATION_NOT_FOUND"};if(st==ChaosAssociationStatus::Provisional||!vp(p))return{false,false,state_version(),"INVALID_ASSOCIATION_REVIEW"};if(a->second.status==st)return{true,true,state_version(),{}};if(a->second.status!=ChaosAssociationStatus::Provisional)return{false,false,state_version(),"CHAOS_ASSOCIATION_REVIEW_TERMINAL"};a->second.status=st;a->second.provenance=p;a->second.history.push_back({st,p});return commit_snapshot(o,k,v,std::move(s),{});}
ChaosResult ChaosClusterLedger::expire_due_associations(const std::string&o,const std::string&k,
 std::uint64_t v,const std::string&id,const std::string&as_of,const ChaosProvenance&p){
 std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);
 if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};
 if(!utc_timestamp(as_of)||as_of.empty()||!vp(p))return{false,false,state_version(),"CHAOS_ASSOCIATION_EXPIRY_INPUT_INVALID"};
 std::size_t expired=0;
 for(auto&item:it->second.associations){auto&a=item.second;
  if(a.valid_until.empty()||a.valid_until>as_of||
     a.status==ChaosAssociationStatus::Expired||a.status==ChaosAssociationStatus::Rejected)continue;
  a.status=ChaosAssociationStatus::Expired;a.provenance=p;
  a.history.push_back({ChaosAssociationStatus::Expired,p});++expired;
 }
 if(expired==0)return{true,true,state_version(),{}};
 return commit_snapshot(o,k,v,std::move(s),{});
}
ChaosResult ChaosClusterLedger::add_pattern_hypothesis(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const ChaosPatternHypothesis&h){std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};if(h.hypothesis_id.empty()||h.statement.empty()||h.member_refs.empty()||!vp(h.provenance)||(h.epistemic_state!="HYPOTHETICAL"&&h.epistemic_state!="UNVERIFIED"&&h.epistemic_state!="CONTRADICTED"&&h.epistemic_state!="REJECTED"))return{false,false,state_version(),"PATTERN_HYPOTHESIS_INVALID"};std::set<std::string>seen;for(const auto&r:h.member_refs)if(!it->second.members.count(r)||!seen.insert(r).second)return{false,false,state_version(),"PATTERN_HYPOTHESIS_MEMBER_INVALID"};for(const auto&r:h.counterexample_refs)if(!it->second.members.count(r))return{false,false,state_version(),"PATTERN_HYPOTHESIS_COUNTEREXAMPLE_INVALID"};auto old=it->second.pattern_hypotheses.find(h.hypothesis_id);if(old!=it->second.pattern_hypotheses.end())return{old->second.statement==h.statement,true,state_version(),old->second.statement==h.statement?std::string():"PATTERN_HYPOTHESIS_ID_CONFLICT"};it->second.pattern_hypotheses.emplace(h.hypothesis_id,h);if(it->second.organization==ChaosOrganization::Unordered)it->second.organization=ChaosOrganization::Provisional;return commit_snapshot(o,k,v,std::move(s),{});}
ChaosResult ChaosClusterLedger::record_observation(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const std::string&dimension,const ChaosScoreObservation&observation){
 std::lock_guard<std::mutex>lock(mutex_);
 State s=state_;auto it=s.find(id);
 if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};
 static const std::set<std::string>allowed={"organization","salience","recognition_confidence","novelty","diversity","ambiguity","utility"};
 if(!allowed.count(dimension)||!std::isfinite(observation.value)||observation.value<0.0||observation.value>1.0||
    observation.scorer_id.empty()||observation.scorer_version.empty()||
    observation.policy_version.empty()||observation.observed_at.empty())
  return{false,false,state_version(),"CHAOS_SCORE_OBSERVATION_INVALID"};
 auto&history=it->second.observations[dimension];
 if(!history.empty()){
  const auto&last=history.back();
  if(last.scorer_id==observation.scorer_id&&last.scorer_version==observation.scorer_version&&
     last.policy_version==observation.policy_version&&last.observed_at==observation.observed_at&&
     last.value==observation.value)return{true,true,state_version(),{}};
 }
 if(history.size()>=100000)return{false,false,state_version(),"CHAOS_SCORE_OBSERVATION_LIMIT"};
 history.push_back(observation);
 return commit_snapshot(o,k,v,std::move(s),{});
}
ChaosResult ChaosClusterLedger::record_entity_observation(const std::string&o,const std::string&k,std::uint64_t v,
 const std::string&id,const std::string&entity_kind,const std::string&entity_ref,
 const ChaosScoreObservation&observation){
 std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);
 if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};
 if(!std::isfinite(observation.value)||observation.value<0.0||observation.value>1.0||
    observation.scorer_id.empty()||observation.scorer_version.empty()||
    observation.policy_version.empty()||observation.observed_at.empty())
  return{false,false,state_version(),"CHAOS_SCORE_OBSERVATION_INVALID"};
 std::vector<ChaosScoreObservation>*history=nullptr;
 if(entity_kind=="MEMBER" ){auto x=it->second.members.find(entity_ref);if(x!=it->second.members.end())history=&x->second.salience_observations;}
 else if(entity_kind=="ASSOCIATION"){auto x=it->second.associations.find(entity_ref);if(x!=it->second.associations.end())history=&x->second.confidence_observations;}
 else if(entity_kind=="HYPOTHESIS"){auto x=it->second.pattern_hypotheses.find(entity_ref);if(x!=it->second.pattern_hypotheses.end())history=&x->second.confidence_observations;}
 else return{false,false,state_version(),"CHAOS_SCORE_ENTITY_KIND_INVALID"};
 if(!history)return{false,false,state_version(),"CHAOS_SCORE_ENTITY_NOT_FOUND"};
 if(!history->empty()){const auto&last=history->back();
  if(last.value==observation.value&&last.scorer_id==observation.scorer_id&&
     last.scorer_version==observation.scorer_version&&last.policy_version==observation.policy_version&&
     last.observed_at==observation.observed_at)return{true,true,state_version(),{}};
 }
 if(history->size()>=100000)return{false,false,state_version(),"CHAOS_SCORE_OBSERVATION_LIMIT"};
 history->push_back(observation);return commit_snapshot(o,k,v,std::move(s),{});
}
ChaosResult ChaosClusterLedger::transition(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,ChaosLifecycle target,const std::string&reason,const ChaosProvenance&p){std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};if(reason.empty()||!vp(p)||!allowed_transition(it->second.lifecycle,target))return{false,false,state_version(),"CHAOS_LIFECYCLE_TRANSITION_INVALID"};if(it->second.lifecycle==target)return{true,true,state_version(),{}};it->second.lifecycle=target;return commit_snapshot(o,k,v,std::move(s),{});}
ChaosResult ChaosClusterLedger::promote_subset(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const std::map<std::string,std::string>&targets,const ChaosProvenance&p){std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};if(targets.empty()||!vp(p)||(it->second.lifecycle!=ChaosLifecycle::Reviewable&&it->second.lifecycle!=ChaosLifecycle::Promoted))return{false,false,state_version(),"CHAOS_PROMOTION_INVALID"};for(const auto&x:targets)if(!it->second.members.count(x.first)||x.second.empty()||it->second.promoted_member_targets.count(x.first))return{false,false,state_version(),"CHAOS_PROMOTION_MEMBER_INVALID"};for(const auto&x:targets)it->second.promoted_member_targets.emplace(x);it->second.lifecycle=ChaosLifecycle::Promoted;return commit_snapshot(o,k,v,std::move(s),{});}
ChaosResult ChaosClusterLedger::fork_subset(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const std::string&child_id,const std::vector<std::string>&refs,const ChaosProvenance&p){std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};if(it->second.lifecycle!=ChaosLifecycle::Reviewable||child_id.empty()||s.count(child_id)||refs.empty()||!vp(p))return{false,false,state_version(),"CHAOS_FORK_INVALID"};std::set<std::string>selected;ChaosCluster child;child.cluster_id=child_id;child.lifecycle=ChaosLifecycle::Accumulating;child.organization=it->second.organization;child.creation_provenance=p;child.parent_refs.push_back(id);for(const auto&r:refs){if(!selected.insert(r).second||!it->second.members.count(r))return{false,false,state_version(),"CHAOS_FORK_MEMBER_INVALID"};child.members.emplace(r,it->second.members.at(r));}for(const auto&x:it->second.associations)if(selected.count(x.second.from_ref)&&selected.count(x.second.to_ref))child.associations.emplace(x.first,x.second);for(const auto&x:it->second.pattern_hypotheses){bool all=true;for(const auto&r:x.second.member_refs)if(!selected.count(r))all=false;if(all)child.pattern_hypotheses.emplace(x.first,x.second);}it->second.child_refs.push_back(child_id);it->second.lifecycle=ChaosLifecycle::Forked;s.emplace(child_id,std::move(child));return commit_snapshot(o,k,v,std::move(s),{});}
ChaosResult ChaosClusterLedger::split_subsets(const std::string&o,const std::string&k,std::uint64_t v,const std::string&id,const std::vector<ChaosSplitPart>&parts,const ChaosProvenance&p){std::lock_guard<std::mutex>lock(mutex_);State s=state_;auto it=s.find(id);if(it==s.end())return{false,false,state_version(),"CHAOS_CLUSTER_NOT_FOUND"};if(it->second.lifecycle!=ChaosLifecycle::Reviewable||parts.empty()||!vp(p))return{false,false,state_version(),"CHAOS_SPLIT_INVALID"};std::set<std::string>all;std::set<std::string>child_ids;std::vector<ChaosCluster>children;for(const auto&part:parts){if(part.child_cluster_id.empty()||s.count(part.child_cluster_id)||!child_ids.insert(part.child_cluster_id).second||part.member_refs.empty())return{false,false,state_version(),"CHAOS_SPLIT_PART_INVALID"};ChaosCluster child;child.cluster_id=part.child_cluster_id;child.lifecycle=ChaosLifecycle::Accumulating;child.organization=ChaosOrganization::PartiallyStructured;child.creation_provenance=p;child.parent_refs.push_back(id);std::set<std::string>local;for(const auto&r:part.member_refs){if(!local.insert(r).second||!all.insert(r).second||!it->second.members.count(r))return{false,false,state_version(),"CHAOS_SPLIT_MEMBER_INVALID"};child.members.emplace(r,it->second.members.at(r));}for(const auto&x:it->second.associations)if(local.count(x.second.from_ref)&&local.count(x.second.to_ref))child.associations.emplace(x.first,x.second);for(const auto&x:it->second.pattern_hypotheses){bool subset=true;for(const auto&r:x.second.member_refs)if(!local.count(r))subset=false;if(subset)child.pattern_hypotheses.emplace(x.first,x.second);}children.push_back(std::move(child));}for(const auto&child:children){it->second.child_refs.push_back(child.cluster_id);s.emplace(child.cluster_id,child);}it->second.lifecycle=ChaosLifecycle::Forked;return commit_snapshot(o,k,v,std::move(s),{});}
bool ChaosClusterLedger::get(const std::string&id,ChaosCluster&r)const{std::lock_guard<std::mutex>lock(mutex_);auto i=state_.find(id);if(i==state_.end())return false;r=i->second;return true;}
std::vector<ChaosCluster> ChaosClusterLedger::list()const{std::lock_guard<std::mutex>lock(mutex_);std::vector<ChaosCluster>v;for(const auto&x:state_)v.push_back(x.second);return v;}
std::uint64_t ChaosClusterLedger::state_version()const{return journal_?journal_->state_version():0;}
} // namespace surge
