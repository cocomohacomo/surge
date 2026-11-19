#include "surge/context_graph.hpp"
#include "surge/snapshot_domains.hpp"
#include "surge/sha256.hpp"
#include "surge/scope_validator.hpp"
#include "surge/dependency_engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <utility>
#include <type_traits>

namespace surge {
namespace {
const char* node_kind_name(NodeKind k){switch(k){case NodeKind::Cluster:return "CLUSTER";case NodeKind::ChaosCluster:return "CHAOS_CLUSTER";case NodeKind::Entity:return "ENTITY";case NodeKind::Evidence:return "EVIDENCE";case NodeKind::Claim:return "CLAIM";case NodeKind::Question:return "QUESTION";case NodeKind::QuestionAspect:return "QUESTION_ASPECT";case NodeKind::Dependency:return "DEPENDENCY";case NodeKind::Interest:return "INTEREST";}return "UNKNOWN";}
bool parse_node_kind(const std::string& s,NodeKind& k){const std::pair<const char*,NodeKind> v[]={{"CLUSTER",NodeKind::Cluster},{"CHAOS_CLUSTER",NodeKind::ChaosCluster},{"ENTITY",NodeKind::Entity},{"EVIDENCE",NodeKind::Evidence},{"CLAIM",NodeKind::Claim},{"QUESTION",NodeKind::Question},{"QUESTION_ASPECT",NodeKind::QuestionAspect},{"DEPENDENCY",NodeKind::Dependency},{"INTEREST",NodeKind::Interest}};for(const auto& x:v)if(s==x.first){k=x.second;return true;}return false;}
const char* relation_kind_name(RelationKind k){switch(k){case RelationKind::Supports:return "SUPPORTS";case RelationKind::Contradicts:return "CONTRADICTS";case RelationKind::AgreesWith:return "AGREES_WITH";case RelationKind::ConflictsWith:return "CONFLICTS_WITH";case RelationKind::DependsOn:return "DEPENDS_ON";case RelationKind::About:return "ABOUT";case RelationKind::Supersedes:return "SUPERSEDES";case RelationKind::Retracts:return "RETRACTS";case RelationKind::Duplicates:return "DUPLICATES";case RelationKind::Contains:return "CONTAINS";}return "UNKNOWN";}
bool parse_relation_kind(const std::string& s,RelationKind& k){const std::pair<const char*,RelationKind> v[]={{"SUPPORTS",RelationKind::Supports},{"CONTRADICTS",RelationKind::Contradicts},{"AGREES_WITH",RelationKind::AgreesWith},{"CONFLICTS_WITH",RelationKind::ConflictsWith},{"DEPENDS_ON",RelationKind::DependsOn},{"ABOUT",RelationKind::About},{"SUPERSEDES",RelationKind::Supersedes},{"RETRACTS",RelationKind::Retracts},{"DUPLICATES",RelationKind::Duplicates},{"CONTAINS",RelationKind::Contains}};for(const auto& x:v)if(s==x.first){k=x.second;return true;}return false;}
const char* scope_kind_name(ScopeKind k){switch(k){case ScopeKind::Negation:return "NEGATION";case ScopeKind::Condition:return "CONDITION";case ScopeKind::Modality:return "MODALITY";case ScopeKind::Concession:return "CONCESSION";case ScopeKind::Contrast:return "CONTRAST";case ScopeKind::Coreference:return "COREFERENCE";case ScopeKind::Anaphora:return "ANAPHORA";case ScopeKind::Quotation:return "QUOTATION";case ScopeKind::Temporal:return "TEMPORAL";case ScopeKind::ListContinuation:return "LIST_CONTINUATION";case ScopeKind::TableHeader:return "TABLE_HEADER";}return "UNKNOWN";}
bool parse_scope_kind(const std::string& s,ScopeKind& k){const std::pair<const char*,ScopeKind> v[]={{"NEGATION",ScopeKind::Negation},{"CONDITION",ScopeKind::Condition},{"MODALITY",ScopeKind::Modality},{"CONCESSION",ScopeKind::Concession},{"CONTRAST",ScopeKind::Contrast},{"COREFERENCE",ScopeKind::Coreference},{"ANAPHORA",ScopeKind::Anaphora},{"QUOTATION",ScopeKind::Quotation},{"TEMPORAL",ScopeKind::Temporal},{"LIST_CONTINUATION",ScopeKind::ListContinuation},{"TABLE_HEADER",ScopeKind::TableHeader}};for(const auto& x:v)if(s==x.first){k=x.second;return true;}return false;}
struct ParsedRfc3339 { std::int64_t seconds{0}; int nanos{0}; };
bool parse_rfc3339(const std::string& value, ParsedRfc3339& out) {
 auto digit=[&](std::size_t i)->int { return i<value.size()&&value[i]>='0'&&value[i]<='9'?value[i]-'0':-1; };
 auto number=[&](std::size_t i,std::size_t n,int& result) {
  result=0;for(std::size_t x=0;x<n;++x){const int d=digit(i+x);if(d<0)return false;result=result*10+d;}return true;
 };
 if(value.size()<20||value[4]!='-'||value[7]!='-'||(value[10]!='T'&&value[10]!='t')||value[13]!=':'||value[16]!=':')return false;
 int year=0,month=0,day=0,hour=0,minute=0,second=0;
 if(!number(0,4,year)||!number(5,2,month)||!number(8,2,day)||!number(11,2,hour)||!number(14,2,minute)||!number(17,2,second))return false;
 if(year<1||month<1||month>12||hour>23||minute>59||second>59)return false;
 const bool leap=(year%4==0&&(year%100!=0||year%400==0));
 const int month_days[]={31,leap?29:28,31,30,31,30,31,31,30,31,30,31};
 if(day<1||day>month_days[month-1])return false;
 std::size_t pos=19;int nanos=0;
 if(pos<value.size()&&value[pos]=='.'){
  ++pos;int digits=0;
  while(pos<value.size()&&digit(pos)>=0){if(digits<9)nanos=nanos*10+digit(pos);++digits;++pos;}
  if(digits==0||digits>9)return false;
  while(digits++<9)nanos*=10;
 }
 int offset_seconds=0;
 if(pos<value.size()&&(value[pos]=='Z'||value[pos]=='z'))++pos;
 else if(pos<value.size()&&(value[pos]=='+'||value[pos]=='-')){
  const int sign=value[pos]=='+'?1:-1;++pos;int oh=0,om=0;
  if(pos+5!=value.size()||value[pos+2]!=':'||!number(pos,2,oh)||!number(pos+3,2,om)||oh>23||om>59||(sign<0&&oh==0&&om==0))return false;
  offset_seconds=sign*(oh*3600+om*60);pos+=5;
 } else return false;
 if(pos!=value.size())return false;
 int y=year;y-=month<=2;const int era=(y>=0?y:y-399)/400;
 const unsigned yoe=static_cast<unsigned>(y-era*400);
 const unsigned mp=static_cast<unsigned>(month+(month>2?-3:9));
 const unsigned doy=(153*mp+2)/5+static_cast<unsigned>(day)-1;
 const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
 const std::int64_t days=static_cast<std::int64_t>(era)*146097+doe-719468;
 out.seconds=days*86400+hour*3600+minute*60+second-offset_seconds;out.nanos=nanos;return true;
}
bool time_less(const ParsedRfc3339& a,const ParsedRfc3339& b){
 return a.seconds<b.seconds||(a.seconds==b.seconds&&a.nanos<b.nanos);
}
std::string encode_claim_validity(const ClaimValidityRecord& record) {
 const auto& v=record.validity;std::ostringstream out;
 out<<std::quoted(v.claim_id)<<' '<<std::quoted(v.valid_from)<<' '<<std::quoted(v.valid_until)<<' '
    <<std::quoted(v.basis_evidence_id)<<' '<<v.revision<<' '<<record.recorded_state_version<<' '
    <<std::quoted(v.review_evidence_id)<<' '<<std::quoted(v.reviewer_id);return out.str();
}
std::string encode_evidence_validity(const EvidenceValidityRecord& record) {
 const auto& v=record.validity;std::ostringstream out;
 out<<std::quoted(v.evidence_id)<<' '<<std::quoted(v.valid_from)<<' '<<std::quoted(v.valid_until)<<' '
    <<std::quoted(v.basis_evidence_id)<<' '<<v.revision<<' '<<record.recorded_state_version<<' '
    <<std::quoted(v.review_evidence_id)<<' '<<std::quoted(v.reviewer_id);return out.str();
}
bool decode_evidence_validity(const std::string& payload,EvidenceValidityRecord& record) {
 std::istringstream in(payload);std::string extra;auto& v=record.validity;
 if(!(in>>std::quoted(v.evidence_id)>>std::quoted(v.valid_from)>>std::quoted(v.valid_until)>>
      std::quoted(v.basis_evidence_id)>>v.revision>>record.recorded_state_version))return false;
 if(in>>std::quoted(v.review_evidence_id)){if(!(in>>std::quoted(v.reviewer_id))||(in>>extra))return false;}
 else {in.clear();if(in>>extra)return false;}
 return true;
}
bool decode_claim_validity(const std::string& payload,ClaimValidityRecord& record) {
 std::istringstream in(payload);std::string extra;auto& v=record.validity;
 if(!(in>>std::quoted(v.claim_id)>>std::quoted(v.valid_from)>>std::quoted(v.valid_until)>>
      std::quoted(v.basis_evidence_id)>>v.revision>>record.recorded_state_version))return false;
 if(in>>std::quoted(v.review_evidence_id)){if(!(in>>std::quoted(v.reviewer_id))||(in>>extra))return false;}
 else {in.clear();if(in>>extra)return false;}
 return true;
}
std::string encode_node(const GraphNode& n){std::ostringstream o;o<<std::quoted(n.id)<<' '<<node_kind_name(n.kind)<<' '<<std::quoted(n.content)<<' '<<n.references.size();for(const auto& r:n.references)o<<' '<<std::quoted(r);return o.str();}
bool decode_node(const std::string& p,GraphNode& n){std::istringstream i(p);std::string k;std::size_t c=0;if(!(i>>std::quoted(n.id)>>k>>std::quoted(n.content)>>c)||!parse_node_kind(k,n.kind)||c>1024)return false;for(std::size_t x=0;x<c;++x){std::string r;if(!(i>>std::quoted(r)))return false;n.references.push_back(r);}std::string extra;return !(i>>extra);}
std::string encode_relation(const GraphRelation& r){std::ostringstream o;o<<std::quoted(r.id)<<' '<<relation_kind_name(r.kind)<<' '<<std::quoted(r.from)<<' '<<std::quoted(r.to);return o.str();}
bool decode_relation(const std::string& p,GraphRelation& r){std::istringstream i(p);std::string k,extra;if(!(i>>std::quoted(r.id)>>k>>std::quoted(r.from)>>std::quoted(r.to))||!parse_relation_kind(k,r.kind))return false;return !(i>>extra);}
std::string encode_anchor(const ScopeAnchor& a){std::ostringstream o;o<<std::quoted(a.anchor_id)<<' '<<std::quoted(a.source_digest)<<' '<<std::quoted(a.offset_basis)<<' '<<a.start_offset<<' '<<a.end_offset<<' '<<std::quoted(a.exact_text)<<' '<<std::quoted(a.packet_ref);return o.str();}
bool decode_anchor(std::istream& i,ScopeAnchor& a){return static_cast<bool>(i>>std::quoted(a.anchor_id)>>std::quoted(a.source_digest)>>std::quoted(a.offset_basis)>>a.start_offset>>a.end_offset>>std::quoted(a.exact_text)>>std::quoted(a.packet_ref));}
std::string encode_scope(const AddScopeSpan& add){const auto& s=add.span;std::ostringstream o;o<<std::quoted(s.span_id)<<' '<<scope_kind_name(s.kind)<<' '<<std::quoted(s.source_digest)<<' '<<std::quoted(s.offset_basis)<<' '<<encode_anchor(s.opener)<<' '<<(s.closer?1:0);if(s.closer)o<<' '<<encode_anchor(*s.closer);o<<' '<<s.target_refs.size();for(const auto& x:s.target_refs)o<<' '<<std::quoted(x);o<<' '<<s.antecedent_refs.size();for(const auto& x:s.antecedent_refs)o<<' '<<std::quoted(x);o<<' '<<std::quoted(s.attribution_ref)<<' '<<std::quoted(s.modality_kind)<<' '<<std::quoted(s.temporal_role)<<' '<<std::quoted(s.parent_ref)<<' '<<s.packet_refs.size();for(const auto& x:s.packet_refs)o<<' '<<std::quoted(x);o<<' '<<std::quoted(add.source_snapshot);return o.str();}
bool decode_scope(const std::string& p,AddScopeSpan& add){std::istringstream i(p);auto& s=add.span;std::string kind;int has_closer=0;std::size_t n=0;if(!(i>>std::quoted(s.span_id)>>kind>>std::quoted(s.source_digest)>>std::quoted(s.offset_basis))||!parse_scope_kind(kind,s.kind)||!decode_anchor(i,s.opener)||!(i>>has_closer))return false;if(has_closer!=0&&has_closer!=1)return false;if(has_closer){ScopeAnchor a;if(!decode_anchor(i,a))return false;s.closer=std::move(a);}if(!(i>>n)||n>1024)return false;for(std::size_t x=0;x<n;++x){std::string v;if(!(i>>std::quoted(v)))return false;s.target_refs.push_back(v);}if(!(i>>n)||n>1024)return false;for(std::size_t x=0;x<n;++x){std::string v;if(!(i>>std::quoted(v)))return false;s.antecedent_refs.push_back(v);}if(!(i>>std::quoted(s.attribution_ref)>>std::quoted(s.modality_kind)>>std::quoted(s.temporal_role)>>std::quoted(s.parent_ref)>>n)||n>4096)return false;for(std::size_t x=0;x<n;++x){std::string v;if(!(i>>std::quoted(v)))return false;s.packet_refs.push_back(v);}std::string extra;if(!(i>>std::quoted(add.source_snapshot))||(i>>extra))return false;return true;}
std::string encode_focus(const FocusRecord& f){std::ostringstream o;o<<std::quoted(f.cluster_id)<<' '<<std::quoted(f.interest_id);if(f.chaos_cluster)o<<' '<<1;return o.str();}
bool decode_focus(const std::string& p,FocusRecord& f){std::istringstream i(p);int chaos=0;if(!(i>>std::quoted(f.cluster_id)>>std::quoted(f.interest_id)))return false;if(i>>chaos){if(chaos!=0&&chaos!=1)return false;f.chaos_cluster=chaos!=0;}else i.clear();std::string extra;return !(i>>extra);}
std::string hash_text(const std::string& t){std::uint64_t h=1469598103934665603ULL;for(unsigned char c:t){h^=c;h*=1099511628211ULL;}std::ostringstream o;o<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return o.str();}
std::string reassessment_snapshot_payload(const TemporalReassessmentRecord& r) {
 std::ostringstream out;out<<std::quoted(r.trace_id)<<' '<<std::quoted(r.claim_id)<<' '
 <<std::quoted(r.reference_at)<<' '<<std::quoted(r.decision)<<' '
 <<std::quoted(r.claim_valid_from)<<' '<<std::quoted(r.claim_valid_until)<<' '
 <<std::quoted(r.claim_validity_basis_evidence_id)<<' '<<std::quoted(r.reason_code)<<' '<<r.claim_validity_revision<<' '
 <<r.claim_validity_recorded_state_version<<' '<<r.evaluated_state_version<<' '
 <<r.recorded_state_version<<' '<<r.sequence<<' '<<static_cast<int>(r.lifecycle)<<' '
 <<static_cast<int>(r.claim_temporal_state)<<' '<<r.evidence.size();
 for(const auto& e:r.evidence)out<<' '<<std::quoted(e.evidence_id)<<' '<<std::quoted(e.relation_id)
  <<' '<<static_cast<int>(e.relation)<<' '<<e.validity_revision<<' '
  <<e.validity_recorded_state_version<<' '<<std::quoted(e.valid_from)<<' '
  <<std::quoted(e.valid_until)<<' '<<std::quoted(e.basis_evidence_id)<<' '
  <<static_cast<int>(e.temporal_state)<<' '<<e.eligible;
 out<<' '<<r.active_support_ids.size();for(const auto& id:r.active_support_ids)out<<' '<<std::quoted(id);
 out<<' '<<r.active_contradiction_ids.size();for(const auto& id:r.active_contradiction_ids)out<<' '<<std::quoted(id);
 return out.str();
}
std::string reassessment_event_payload(const TemporalReassessmentRecord& r) {
 std::ostringstream out;out<<std::quoted(r.previous_hash)<<' '<<std::quoted(r.snapshot_digest)<<' '
 <<std::quoted(r.chain_hash)<<' '<<std::quoted(reassessment_snapshot_payload(r));return out.str();
}
bool decode_reassessment(const std::string& payload,TemporalReassessmentRecord& r) {
 std::istringstream in(payload);std::string snapshot,extra;
 if(!(in>>std::quoted(r.previous_hash)>>std::quoted(r.snapshot_digest)>>
      std::quoted(r.chain_hash)>>std::quoted(snapshot))||(in>>extra))return false;
 std::istringstream s(snapshot);int lifecycle=0,claim_state=0;std::size_t n=0;
 if(!(s>>std::quoted(r.trace_id)>>std::quoted(r.claim_id)>>std::quoted(r.reference_at)>>
  std::quoted(r.decision)>>std::quoted(r.claim_valid_from)>>std::quoted(r.claim_valid_until)>>
  std::quoted(r.claim_validity_basis_evidence_id)>>std::quoted(r.reason_code)>>r.claim_validity_revision>>r.claim_validity_recorded_state_version>>
  r.evaluated_state_version>>r.recorded_state_version>>r.sequence>>lifecycle>>claim_state>>n)||n>4096)return false;
 if(lifecycle<0||lifecycle>2||claim_state<0||claim_state>3)return false;
 r.lifecycle=static_cast<ClaimStatus>(lifecycle);r.claim_temporal_state=static_cast<ClaimTemporalState>(claim_state);
 for(std::size_t i=0;i<n;++i){ReassessmentEvidenceSnapshot e;int rel=0,state=0;
  if(!(s>>std::quoted(e.evidence_id)>>std::quoted(e.relation_id)>>rel>>
    e.validity_revision>>e.validity_recorded_state_version>>std::quoted(e.valid_from)>>
    std::quoted(e.valid_until)>>std::quoted(e.basis_evidence_id)>>state>>e.eligible)||rel<0||rel>9||state<0||state>3)return false;
  e.relation=static_cast<RelationKind>(rel);e.temporal_state=static_cast<ClaimTemporalState>(state);r.evidence.push_back(e);}
 if(!(s>>n)||n>4096)return false;
 for(std::size_t i=0;i<n;++i){std::string id;if(!(s>>std::quoted(id)))return false;r.active_support_ids.push_back(id);}
 if(!(s>>n)||n>4096)return false;
 for(std::size_t i=0;i<n;++i){std::string id;if(!(s>>std::quoted(id)))return false;r.active_contradiction_ids.push_back(id);}
 return !(s>>extra);
}
std::string encode_dependency_evaluation(const DependencyEvaluationRecord& r) {
 std::ostringstream out;
 out << std::quoted(r.delta_id) << ' ' << r.triggered.size();
 for (const auto& item : r.triggered)
  out << ' ' << std::quoted(item.dependency_id) << ' '
      << std::quoted(item.prerequisite_id) << ' ' << std::quoted(item.dependent_id);
 out << ' ' << r.impacted_nodes.size();
 for (const auto& id : r.impacted_nodes) out << ' ' << std::quoted(id);
 out << ' ' << r.rejected_dependencies.size();
 for (const auto& id : r.rejected_dependencies) out << ' ' << std::quoted(id);
 return out.str();
}
} // namespace

ContextGraph::ContextGraph(std::string p):owned_journal_(std::make_unique<EventJournal>(std::move(p))),journal_(owned_journal_.get()){}
ContextGraph::ContextGraph(EventJournal& j):journal_(&j){}
bool ContextGraph::open(){if(!journal_||!journal_->open())return false;return replay();}

bool ContextGraph::apply_node(State& s,const GraphNode& n,std::string& e)const{
 if(n.id.empty()||(n.content.empty()&&n.kind!=NodeKind::Dependency)||s.nodes.count(n.id)){e="NODE_ID_OR_CONTENT_INVALID";return false;}
 auto ref_kind=[&](std::size_t x,NodeKind k){
  if(x>=n.references.size())return false;
  const auto found=s.nodes.find(n.references[x]);
  return found!=s.nodes.end()&&found->second.kind==k;
 };
 for(const auto& r:n.references)if(!s.nodes.count(r)){e="NODE_REFERENCE_NOT_FOUND";return false;}
 switch(n.kind){
 case NodeKind::Cluster:case NodeKind::ChaosCluster:case NodeKind::Entity:case NodeKind::Evidence:case NodeKind::Question:if(!n.references.empty()){e="NODE_REFERENCE_SHAPE_INVALID";return false;}break;
 case NodeKind::Claim:
  if(n.references.empty()){e="CLAIM_REQUIRES_EVIDENCE";return false;}
  for(const auto& r:n.references){const auto found=s.nodes.find(r);
   if(found==s.nodes.end()||found->second.kind!=NodeKind::Evidence){e="CLAIM_EVIDENCE_REF_INVALID";return false;}}
  break;
 case NodeKind::QuestionAspect:if(n.references.size()!=1||!ref_kind(0,NodeKind::Question)){e="QUESTION_ASPECT_PARENT_INVALID";return false;}break;
 case NodeKind::Dependency:if(n.references.size()!=2||n.references[0]==n.references[1]){e="DEPENDENCY_ENDPOINTS_INVALID";return false;}break;
 case NodeKind::Interest:if(n.references.size()!=2||!ref_kind(0,NodeKind::QuestionAspect)||!(ref_kind(1,NodeKind::Cluster)||ref_kind(1,NodeKind::ChaosCluster))){e="INTEREST_TARGET_INVALID";return false;}break;
 }s.nodes.emplace(n.id,n);if(n.kind==NodeKind::Claim)s.claim_statuses.emplace(n.id,ClaimStatus::Active);return true;
}
bool ContextGraph::apply_relation(State& s,const GraphRelation& r,std::string& e)const{
 if(r.id.empty()||s.relations.count(r.id)){e="RELATION_ID_INVALID_OR_DUPLICATE";return false;}
 if(r.from==r.to||!s.nodes.count(r.from)||!s.nodes.count(r.to)){e="RELATION_ENDPOINT_INVALID";return false;}
 const auto from=s.nodes.find(r.from),to=s.nodes.find(r.to);
 if(from==s.nodes.end()||to==s.nodes.end()){e="RELATION_ENDPOINT_INVALID";return false;}
 auto a=from->second.kind,b=to->second.kind;
 switch(r.kind){case RelationKind::Supports:if(!((a==NodeKind::Claim&&b==NodeKind::Claim)||(a==NodeKind::Evidence&&b==NodeKind::Claim))){e="CLAIM_RELATION_TYPE_INVALID";return false;}break;case RelationKind::Contradicts:if(!((a==NodeKind::Claim&&b==NodeKind::Claim)||(a==NodeKind::Evidence&&b==NodeKind::Claim))){e="CLAIM_RELATION_TYPE_INVALID";return false;}break;
 case RelationKind::AgreesWith:case RelationKind::ConflictsWith:if(a!=b||(a!=NodeKind::Cluster&&a!=NodeKind::Claim&&a!=NodeKind::Question)){e="GRAPH_RELATION_TYPE_INVALID";return false;}break;
 case RelationKind::Duplicates:if(a!=NodeKind::Question||b!=NodeKind::Question){e="QUESTION_RELATION_TYPE_INVALID";return false;}break;
 case RelationKind::Contains:
  if(!((a==NodeKind::Question&&b==NodeKind::Question)||
       (a==NodeKind::Cluster&&(b==NodeKind::Cluster||b==NodeKind::Claim||
                               b==NodeKind::Entity||b==NodeKind::Evidence)))){
   e="CLUSTER_CONTAINMENT_TYPE_INVALID";return false;
  }
  break;
 case RelationKind::About:if((a!=NodeKind::Claim&&a!=NodeKind::Question&&a!=NodeKind::QuestionAspect)||(b!=NodeKind::Entity&&b!=NodeKind::Cluster)){e="ABOUT_RELATION_TYPE_INVALID";return false;}break;
 case RelationKind::DependsOn:if(a==NodeKind::Question&&b==NodeKind::Question){
   std::vector<std::string> pending{r.to};std::set<std::string> visited;
   while(!pending.empty()){auto current=pending.back();pending.pop_back();if(current==r.from){e="QUESTION_DEPENDENCY_CYCLE";return false;}if(!visited.insert(current).second)continue;if(visited.size()>4096){e="QUESTION_DEPENDENCY_TRAVERSAL_LIMIT";return false;}for(const auto& edge:s.relations)if(edge.second.kind==RelationKind::DependsOn&&edge.second.from==current){const auto target=s.nodes.find(edge.second.to);if(target!=s.nodes.end()&&target->second.kind==NodeKind::Question)pending.push_back(edge.second.to);}}
 }break;
 case RelationKind::Supersedes:{if(a!=NodeKind::Claim||b!=NodeKind::Claim){e="CLAIM_SUPERSESSION_ENDPOINT_INVALID";return false;}const auto old_status=s.claim_statuses.find(r.to);const auto new_status=s.claim_statuses.find(r.from);if(old_status==s.claim_statuses.end()||new_status==s.claim_statuses.end()||old_status->second!=ClaimStatus::Active||new_status->second!=ClaimStatus::Active){e="CLAIM_NOT_ACTIVE_FOR_SUPERSESSION";return false;}break;}
 case RelationKind::Retracts:{if(a!=NodeKind::Evidence||b!=NodeKind::Claim){e="CLAIM_RETRACTION_ENDPOINT_INVALID";return false;}const auto status=s.claim_statuses.find(r.to);if(status==s.claim_statuses.end()||status->second!=ClaimStatus::Active){e="CLAIM_NOT_ACTIVE_FOR_RETRACTION";return false;}break;}}
 s.relations.emplace(r.id,r);if(r.kind==RelationKind::Supersedes)s.claim_statuses[r.to]=ClaimStatus::Superseded;else if(r.kind==RelationKind::Retracts)s.claim_statuses[r.to]=ClaimStatus::Retracted;return true;
}
bool ContextGraph::apply_scope_span(State& s,const AddScopeSpan& add,std::string& e)const{
 if(s.scope_spans.count(add.span.span_id)){e="SCOPE_SPAN_ID_DUPLICATE";return false;}
 const auto result=ScopeSpanValidator::validate(add.span,add.source_snapshot,s.nodes);
 if(!result.accepted){e=result.error.empty()?"SCOPE_SPAN_REJECTED":result.error;return false;}
 s.scope_spans.emplace(add.span.span_id,ScopeSpanRecord{add.span,result.status,result.effect,result.may_emit_unconditional_claim});return true;
}
bool ContextGraph::apply_claim_validity(State& s,const ClaimValidity& validity,
    std::uint64_t recorded_state_version,std::string& error)const{
 const auto claim=s.nodes.find(validity.claim_id);
 const auto evidence=s.nodes.find(validity.basis_evidence_id);
 if(claim==s.nodes.end()||claim->second.kind!=NodeKind::Claim){error="CLAIM_VALIDITY_CLAIM_INVALID";return false;}
 if(evidence==s.nodes.end()||evidence->second.kind!=NodeKind::Evidence){error="CLAIM_VALIDITY_EVIDENCE_INVALID";return false;}
 const bool directly_linked=std::find(claim->second.references.begin(),claim->second.references.end(),validity.basis_evidence_id)!=claim->second.references.end();
 bool supported_by_relation=false;for(const auto& item:s.relations){const auto& rel=item.second;if(rel.kind==RelationKind::Supports&&rel.from==validity.basis_evidence_id&&rel.to==validity.claim_id){supported_by_relation=true;break;}}
 if(!directly_linked&&!supported_by_relation){error="CLAIM_VALIDITY_EVIDENCE_NOT_BOUND_TO_CLAIM";return false;}
 if(validity.review_evidence_id.empty()!=validity.reviewer_id.empty()){error="CLAIM_VALIDITY_REVIEW_PROVENANCE_INVALID";return false;}
 if(!validity.review_evidence_id.empty()){const auto review=s.nodes.find(validity.review_evidence_id);if(review==s.nodes.end()||review->second.kind!=NodeKind::Evidence){error="CLAIM_VALIDITY_REVIEW_EVIDENCE_INVALID";return false;}}
 ParsedRfc3339 from{},until{};
 if(!parse_rfc3339(validity.valid_from,from)||(!validity.valid_until.empty()&&!parse_rfc3339(validity.valid_until,until))){error="CLAIM_VALIDITY_TIMESTAMP_INVALID";return false;}
 if(!validity.valid_until.empty()&&!time_less(from,until)){error="CLAIM_VALIDITY_INTERVAL_INVALID";return false;}
 auto& history=s.claim_validity_history[validity.claim_id];
 if(validity.revision!=history.size()+1||recorded_state_version==0){error="CLAIM_VALIDITY_REVISION_INVALID";return false;}
 history.push_back({validity,recorded_state_version});return true;
}
bool ContextGraph::apply_evidence_validity(State& s,const EvidenceValidity& validity,
    std::uint64_t recorded_state_version,std::string& error)const{
 const auto subject=s.nodes.find(validity.evidence_id);
 const auto basis=s.nodes.find(validity.basis_evidence_id);
 if(subject==s.nodes.end()||subject->second.kind!=NodeKind::Evidence){error="EVIDENCE_VALIDITY_EVIDENCE_INVALID";return false;}
 if(basis==s.nodes.end()||basis->second.kind!=NodeKind::Evidence){error="EVIDENCE_VALIDITY_BASIS_INVALID";return false;}
 bool basis_bound=validity.evidence_id==validity.basis_evidence_id;
 for(const auto& item:s.relations){const auto& rel=item.second;
  if(rel.kind==RelationKind::Supports&&rel.from==validity.basis_evidence_id&&rel.to==validity.evidence_id){basis_bound=true;break;}
 }
 if(!basis_bound){error="EVIDENCE_VALIDITY_BASIS_NOT_BOUND";return false;}
 if(validity.review_evidence_id.empty()!=validity.reviewer_id.empty()){error="EVIDENCE_VALIDITY_REVIEW_PROVENANCE_INVALID";return false;}
 if(!validity.review_evidence_id.empty()){
  const auto review=s.nodes.find(validity.review_evidence_id);
  if(review==s.nodes.end()||review->second.kind!=NodeKind::Evidence){error="EVIDENCE_VALIDITY_REVIEW_EVIDENCE_INVALID";return false;}
 }
 ParsedRfc3339 from{},until{};
 if(!parse_rfc3339(validity.valid_from,from)||(!validity.valid_until.empty()&&!parse_rfc3339(validity.valid_until,until))){error="EVIDENCE_VALIDITY_TIMESTAMP_INVALID";return false;}
 if(!validity.valid_until.empty()&&!time_less(from,until)){error="EVIDENCE_VALIDITY_INTERVAL_INVALID";return false;}
 auto& history=s.evidence_validity_history[validity.evidence_id];
 if(validity.revision!=history.size()+1||recorded_state_version==0){error="EVIDENCE_VALIDITY_REVISION_INVALID";return false;}
 history.push_back({validity,recorded_state_version});return true;
}
bool ContextGraph::build_reassessment(const State& s,const std::string& claim_id,
    const std::string& reference_at,std::uint64_t evaluated_version,
    TemporalReassessmentRecord& out,std::string& error)const{
 const auto claim=s.nodes.find(claim_id);
 if(claim==s.nodes.end()||claim->second.kind!=NodeKind::Claim){error="REASSESSMENT_CLAIM_NOT_FOUND";return false;}
 ParsedRfc3339 instant{};if(!parse_rfc3339(reference_at,instant)){error="REASSESSMENT_REFERENCE_TIME_INVALID";return false;}
 out={};out.claim_id=claim_id;out.reference_at=reference_at;out.evaluated_state_version=evaluated_version;
 const auto status=s.claim_statuses.find(claim_id);out.lifecycle=status==s.claim_statuses.end()?ClaimStatus::Active:status->second;
 const auto cv=s.claim_validity_history.find(claim_id);
 if(cv!=s.claim_validity_history.end()&&!cv->second.empty()){
  const auto& latest=cv->second.back();out.claim_validity_revision=latest.validity.revision;
  out.claim_validity_recorded_state_version=latest.recorded_state_version;
  out.claim_valid_from=latest.validity.valid_from;out.claim_valid_until=latest.validity.valid_until;
  out.claim_validity_basis_evidence_id=latest.validity.basis_evidence_id;
  out.claim_temporal_state=evaluate_claim_validity(latest,reference_at);
 }
 for(const auto& item:s.relations){const auto& rel=item.second;
  const auto source_node=s.nodes.find(rel.from);
  if(rel.to!=claim_id||source_node==s.nodes.end()||source_node->second.kind!=NodeKind::Evidence||
    (rel.kind!=RelationKind::Supports&&rel.kind!=RelationKind::Contradicts&&rel.kind!=RelationKind::Retracts))continue;
  ReassessmentEvidenceSnapshot e;e.evidence_id=rel.from;e.relation_id=rel.id;e.relation=rel.kind;
  const auto ev=s.evidence_validity_history.find(e.evidence_id);
  if(ev!=s.evidence_validity_history.end()&&!ev->second.empty()){
   const auto& latest=ev->second.back();e.validity_revision=latest.validity.revision;
   e.validity_recorded_state_version=latest.recorded_state_version;
   e.valid_from=latest.validity.valid_from;e.valid_until=latest.validity.valid_until;
   e.basis_evidence_id=latest.validity.basis_evidence_id;
   ClaimValidityRecord compatible;compatible.validity.valid_from=e.valid_from;compatible.validity.valid_until=e.valid_until;
   e.temporal_state=evaluate_claim_validity(compatible,reference_at);
  }
  e.eligible=out.lifecycle==ClaimStatus::Active&&out.claim_temporal_state==ClaimTemporalState::Valid&&
      e.temporal_state==ClaimTemporalState::Valid;
  if(e.eligible&&rel.kind==RelationKind::Supports)out.active_support_ids.push_back(e.evidence_id);
  if(e.eligible&&rel.kind==RelationKind::Contradicts)out.active_contradiction_ids.push_back(e.evidence_id);
  out.evidence.push_back(std::move(e));
 }
 std::sort(out.evidence.begin(),out.evidence.end(),[](const auto& a,const auto& b){return a.relation_id<b.relation_id;});
 std::sort(out.active_support_ids.begin(),out.active_support_ids.end());
 std::sort(out.active_contradiction_ids.begin(),out.active_contradiction_ids.end());
 if(out.lifecycle!=ClaimStatus::Active){out.decision="CLAIM_INACTIVE";out.reason_code="CLAIM_LIFECYCLE_NOT_ACTIVE";}
 else if(out.claim_temporal_state!=ClaimTemporalState::Valid){out.decision="CLAIM_NOT_CURRENT";out.reason_code="CLAIM_VALIDITY_NOT_ACTIVE_AT_REFERENCE";}
 else if(!out.active_support_ids.empty()&&!out.active_contradiction_ids.empty()){out.decision="TEMPORAL_CONFLICT";out.reason_code="ACTIVE_SUPPORT_AND_CONTRADICTION";}
 else if(!out.active_contradiction_ids.empty()){out.decision="CONTRADICTED";out.reason_code="ACTIVE_CONTRADICTION";}
 else if(!out.active_support_ids.empty()){out.decision="SUPPORTED";out.reason_code="ACTIVE_SUPPORT";}
 else{out.decision="INSUFFICIENT_CURRENT_EVIDENCE";out.reason_code="NO_ELIGIBLE_CURRENT_EVIDENCE";}
 return true;
}
bool ContextGraph::apply_reassessment(State& s,const TemporalReassessmentRecord& r,std::string& error)const{
 if(r.claim_id.empty()||r.trace_id.empty()||r.sequence==0||r.recorded_state_version==0||
    !s.nodes.count(r.claim_id)||s.nodes.at(r.claim_id).kind!=NodeKind::Claim){error="REASSESSMENT_RECORD_INVALID";return false;}
 auto& history=s.reassessment_history[r.claim_id];
 const std::uint64_t expected_sequence=history.size()+1;
 const std::string previous=history.empty()?std::string(64,'0'):history.back().chain_hash;
 if(r.sequence!=expected_sequence||r.previous_hash!=previous||
    r.snapshot_digest!=sha256_hex(reassessment_snapshot_payload(r))||
    r.chain_hash!=sha256_hex(r.previous_hash+r.snapshot_digest)){
  error="REASSESSMENT_CHAIN_INVALID";return false;
 }
 TemporalReassessmentRecord expected;
 if(!build_reassessment(s,r.claim_id,r.reference_at,r.evaluated_state_version,expected,error))return false;
 expected.trace_id=r.trace_id;expected.recorded_state_version=r.recorded_state_version;expected.sequence=r.sequence;
 expected.previous_hash=r.previous_hash;expected.snapshot_digest=r.snapshot_digest;expected.chain_hash=r.chain_hash;
 if(reassessment_snapshot_payload(expected)!=reassessment_snapshot_payload(r)){error="REASSESSMENT_SNAPSHOT_STALE_OR_TAMPERED";return false;}
 history.push_back(r);return true;
}
bool ContextGraph::reassess_claim(const std::string& claim_id,const std::string& reference_at,
    std::uint64_t expected_state_version,TemporalReassessmentRecord& result,std::string& error){
 error.clear();if(expected_state_version!=state_version()){error="STATE_VERSION_CONFLICT";return false;}
 TemporalReassessmentRecord record;
 if(!build_reassessment(state_,claim_id,reference_at,expected_state_version,record,error))return false;
 const auto it=state_.reassessment_history.find(claim_id);
 const std::size_t previous_count=it==state_.reassessment_history.end()?0:it->second.size();
 record.sequence=previous_count+1;record.recorded_state_version=expected_state_version+1;
 record.previous_hash=previous_count==0?std::string(64,'0'):it->second.back().chain_hash;
 record.trace_id="reassessment:"+claim_id+":"+std::to_string(record.sequence);
 record.snapshot_digest=sha256_hex(reassessment_snapshot_payload(record));
 record.chain_hash=sha256_hex(record.previous_hash+record.snapshot_digest);
 const auto payload=reassessment_event_payload(record);
 const auto committed=journal_->commit(record.trace_id,record.trace_id,expected_state_version,
    {{record.trace_id,"TEMPORAL_REASSESSMENT_V1",payload}});
 if(!committed.committed){error=committed.error;return false;}
 if(!apply_reassessment(state_,record,error))return false;
 result=record;return true;
}
bool ContextGraph::apply_focus(State& s,const FocusRecord& f,std::string& e)const{
 const auto target=s.nodes.find(f.cluster_id);
 const auto expected=f.chaos_cluster?NodeKind::ChaosCluster:NodeKind::Cluster;
 if(target==s.nodes.end()||target->second.kind!=expected){e=f.chaos_cluster?"FOCUS_CHAOS_CLUSTER_INVALID":"FOCUS_CLUSTER_INVALID";return false;}
 if(!f.interest_id.empty()){
  const auto interest=s.nodes.find(f.interest_id);
  if(interest==s.nodes.end()||interest->second.kind!=NodeKind::Interest||
     interest->second.references.size()!=2||interest->second.references[1]!=f.cluster_id){
   e="FOCUS_INTEREST_INVALID";return false;
  }
 }
 s.focus=f;s.focus_history.push_back(f);return true;
}
std::string ContextGraph::fingerprint(const SemanticDelta& d)const{
 std::ostringstream o;o<<std::quoted(d.delta_id)<<' '<<d.expected_state_version<<' ';
 for(const auto& n:d.nodes)o<<"N "<<encode_node(n.node)<<'\n';
 for(const auto& r:d.relations)o<<"R "<<encode_relation(r.relation)<<'\n';
 for(const auto& s:d.scope_spans)o<<"S "<<encode_scope(s)<<'\n';
 for(const auto& v:d.claim_validity_updates)o<<"V "<<std::quoted(v.claim_id)<<' '<<std::quoted(v.valid_from)<<' '<<std::quoted(v.valid_until)<<' '<<std::quoted(v.basis_evidence_id)<<' '<<v.revision<<' '<<std::quoted(v.review_evidence_id)<<' '<<std::quoted(v.reviewer_id)<<'\n';
 for(const auto& v:d.evidence_validity_updates)o<<"EV "<<std::quoted(v.evidence_id)<<' '<<std::quoted(v.valid_from)<<' '<<std::quoted(v.valid_until)<<' '<<std::quoted(v.basis_evidence_id)<<' '<<v.revision<<' '<<std::quoted(v.review_evidence_id)<<' '<<std::quoted(v.reviewer_id)<<'\n';
 if(d.focus) { o<<"F "<<encode_focus(d.focus->focus)<<'\n'; }
 return hash_text(o.str());
}
std::vector<JournalEvent> ContextGraph::encode(const SemanticDelta& d,const std::string& fp)const{
 std::vector<JournalEvent> out;std::ostringstream m;m<<std::quoted(d.delta_id)<<' '<<std::quoted(d.idempotency_key)<<' '<<d.expected_state_version<<' '<<std::quoted(fp);
 out.push_back({d.delta_id+"/meta","DELTA_META",m.str()});std::size_t i=0;
 for(const auto& n:d.nodes)out.push_back({d.delta_id+"/node/"+std::to_string(i++),"NODE_ADD",encode_node(n.node)});
 i=0;for(const auto& r:d.relations)out.push_back({d.delta_id+"/relation/"+std::to_string(i++),"RELATION_ADD",encode_relation(r.relation)});
 i=0;for(const auto& s:d.scope_spans)out.push_back({d.delta_id+"/scope/"+std::to_string(i++),"SCOPE_SPAN_ADD",encode_scope(s)});
 if(d.focus) { out.push_back({d.delta_id+"/focus","FOCUS_SET",encode_focus(d.focus->focus)}); }
 i=0;for(const auto& v:d.claim_validity_updates){ClaimValidityRecord record{v,d.expected_state_version+1};out.push_back({d.delta_id+"/claim-validity/"+std::to_string(i++),"CLAIM_VALIDITY_SET",encode_claim_validity(record)});}
 i=0;for(const auto& v:d.evidence_validity_updates){EvidenceValidityRecord record{v,d.expected_state_version+1};out.push_back({d.delta_id+"/evidence-validity/"+std::to_string(i++),"EVIDENCE_VALIDITY_SET",encode_evidence_validity(record)});}
 return out;
}
bool ContextGraph::replay_event(State& s,const JournalEvent& e,std::string& error)const{
 if(e.kind=="NODE_ADD"){GraphNode n;if(!decode_node(e.payload,n)){error="JOURNAL_NODE_DECODE_FAILED";return false;}return apply_node(s,n,error);}
 if(e.kind=="RELATION_ADD"){GraphRelation r;if(!decode_relation(e.payload,r)){error="JOURNAL_RELATION_DECODE_FAILED";return false;}return apply_relation(s,r,error);}
 if(e.kind=="SCOPE_SPAN_ADD"){AddScopeSpan span;if(!decode_scope(e.payload,span)){error="JOURNAL_SCOPE_SPAN_DECODE_FAILED";return false;}return apply_scope_span(s,span,error);}
 if(e.kind=="FOCUS_SET"){FocusRecord f;if(!decode_focus(e.payload,f)){error="JOURNAL_FOCUS_DECODE_FAILED";return false;}return apply_focus(s,f,error);}
 if(e.kind=="CLAIM_VALIDITY_SET"){ClaimValidityRecord record;if(!decode_claim_validity(e.payload,record)){error="JOURNAL_CLAIM_VALIDITY_DECODE_FAILED";return false;}return apply_claim_validity(s,record.validity,record.recorded_state_version,error);}
 if(e.kind=="EVIDENCE_VALIDITY_SET"){EvidenceValidityRecord record;if(!decode_evidence_validity(e.payload,record)){error="JOURNAL_EVIDENCE_VALIDITY_DECODE_FAILED";return false;}return apply_evidence_validity(s,record.validity,record.recorded_state_version,error);}
 if(e.kind=="TEMPORAL_REASSESSMENT_V1"){TemporalReassessmentRecord record;if(!decode_reassessment(e.payload,record)){error="JOURNAL_REASSESSMENT_DECODE_FAILED";return false;}return apply_reassessment(s,record,error);}
 if(e.kind=="DEPENDENCY_EVALUATION"){
  std::istringstream in(e.payload); DependencyEvaluationRecord record;
  std::size_t count=0;
  if(!(in>>std::quoted(record.delta_id)>>count)||record.delta_id.empty()||
     count>4096||s.dependency_evaluations.count(record.delta_id)){
   error="JOURNAL_DEPENDENCY_EVALUATION_INVALID";return false;
  }
  for(std::size_t i=0;i<count;++i){
   DependencyTrigger trigger;
   if(!(in>>std::quoted(trigger.dependency_id)>>std::quoted(trigger.prerequisite_id)>>
        std::quoted(trigger.dependent_id))||
      !s.nodes.count(trigger.dependency_id)){
    error="JOURNAL_DEPENDENCY_TRIGGER_INVALID";return false;
   }
   const auto dependency=s.nodes.find(trigger.dependency_id);
   if(dependency->second.kind!=NodeKind::Dependency||
      dependency->second.references.size()!=2||
      dependency->second.references[0]!=trigger.prerequisite_id||
      dependency->second.references[1]!=trigger.dependent_id){
    error="JOURNAL_DEPENDENCY_TRIGGER_INVALID";return false;
   }
   record.triggered.push_back(std::move(trigger));
  }
  if(!(in>>count)||count>4096){error="JOURNAL_DEPENDENCY_IMPACT_INVALID";return false;}
  for(std::size_t i=0;i<count;++i){std::string id;if(!(in>>std::quoted(id))||!s.nodes.count(id)){error="JOURNAL_DEPENDENCY_IMPACT_INVALID";return false;}record.impacted_nodes.push_back(std::move(id));}
  if(!(in>>count)||count>4096){error="JOURNAL_DEPENDENCY_REJECTION_INVALID";return false;}
  for(std::size_t i=0;i<count;++i){
   std::string id;if(!(in>>std::quoted(id))){error="JOURNAL_DEPENDENCY_REJECTION_INVALID";return false;}
   const auto dependency=s.nodes.find(id);
   if(dependency==s.nodes.end()||dependency->second.kind!=NodeKind::Dependency){error="JOURNAL_DEPENDENCY_REJECTION_INVALID";return false;}
   record.rejected_dependencies.push_back(std::move(id));
  }
  std::string extra;if(in>>extra){error="JOURNAL_DEPENDENCY_EVALUATION_TRAILING_DATA";return false;}
  s.dependency_evaluations.emplace(record.delta_id,std::move(record));return true;
 }
 if(e.kind=="QUESTION_MEMORY_REFERENCED"){
  std::istringstream in(e.payload);std::string id,extra;std::uint64_t count=0;if(!(in>>std::quoted(id)>>count)||(in>>extra)||count==0){error="JOURNAL_QUESTION_REFERENCE_INVALID";return false;}const auto q=s.nodes.find(id);if(q==s.nodes.end()||q->second.kind!=NodeKind::Question){error="JOURNAL_QUESTION_REFERENCE_INVALID";return false;}const auto previous=s.question_reference_counts[id];if(count!=previous+1){error="JOURNAL_QUESTION_REFERENCE_SEQUENCE_INVALID";return false;}s.question_reference_counts[id]=count;if(s.question_memory_states[id]==QuestionMemoryState::Dormant)s.question_memory_states[id]=QuestionMemoryState::Active;return true;
 }
 if(e.kind=="QUESTION_MEMORY_REFERENCE_CASCADE"){
  std::istringstream in(e.payload);std::string root,extra;std::uint64_t count=0;std::size_t dependent_count=0;
  if(!(in>>std::quoted(root)>>count>>dependent_count)||count==0||dependent_count>4096){error="JOURNAL_QUESTION_REFERENCE_CASCADE_INVALID";return false;}
  const auto q=s.nodes.find(root);if(q==s.nodes.end()||q->second.kind!=NodeKind::Question){error="JOURNAL_QUESTION_REFERENCE_CASCADE_INVALID";return false;}
  const auto previous=s.question_reference_counts[root];if(count!=previous+1){error="JOURNAL_QUESTION_REFERENCE_SEQUENCE_INVALID";return false;}
  std::vector<std::string> dependents;std::set<std::string> unique;
  for(std::size_t i=0;i<dependent_count;++i){std::string id;if(!(in>>std::quoted(id))||!unique.insert(id).second){error="JOURNAL_QUESTION_REFERENCE_CASCADE_INVALID";return false;}const auto d=s.nodes.find(id);if(d==s.nodes.end()||d->second.kind!=NodeKind::Question||s.question_memory_states[id]!=QuestionMemoryState::Dormant){error="JOURNAL_QUESTION_REFERENCE_CASCADE_INVALID";return false;}dependents.push_back(std::move(id));}
  if(in>>extra){error="JOURNAL_QUESTION_REFERENCE_CASCADE_INVALID";return false;}
  s.question_reference_counts[root]=count;s.question_memory_states[root]=QuestionMemoryState::Active;for(const auto& id:dependents)s.question_memory_states[id]=QuestionMemoryState::Active;return true;
 }
 if(e.kind=="QUESTION_MEMORY_STATE_SET"){
  std::istringstream in(e.payload);std::string id,status,extra;if(!(in>>std::quoted(id)>>status)||(in>>extra)){error="JOURNAL_QUESTION_MEMORY_STATE_INVALID";return false;}const auto q=s.nodes.find(id);if(q==s.nodes.end()||q->second.kind!=NodeKind::Question){error="JOURNAL_QUESTION_MEMORY_STATE_INVALID";return false;}QuestionMemoryState v;if(status=="ACTIVE")v=QuestionMemoryState::Active;else if(status=="DORMANT")v=QuestionMemoryState::Dormant;else if(status=="ARCHIVED")v=QuestionMemoryState::Archived;else{error="JOURNAL_QUESTION_MEMORY_STATE_INVALID";return false;}s.question_memory_states[id]=v;return true;
 }
 if(e.kind=="QUESTION_ASPECT_STATUS_BATCH_SET"){
  std::istringstream in(e.payload);std::size_t count=0;
  if(!(in>>count)||count==0||count>4096){error="JOURNAL_QUESTION_ASPECT_BATCH_INVALID";return false;}
  std::map<std::string,QuestionAspectStatus> updates;
  for(std::size_t i=0;i<count;++i){
   std::string id,status;if(!(in>>std::quoted(id)>>status)||id.empty()){error="JOURNAL_QUESTION_ASPECT_BATCH_INVALID";return false;}
   const auto aspect=s.nodes.find(id);if(aspect==s.nodes.end()||aspect->second.kind!=NodeKind::QuestionAspect){error="JOURNAL_QUESTION_ASPECT_BATCH_INVALID";return false;}
   QuestionAspectStatus value;if(status=="OPEN")value=QuestionAspectStatus::Open;else if(status=="RESOLVED")value=QuestionAspectStatus::Resolved;else if(status=="DEFERRED")value=QuestionAspectStatus::Deferred;else{error="JOURNAL_QUESTION_ASPECT_BATCH_INVALID";return false;}
   if(!updates.emplace(id,value).second){error="JOURNAL_QUESTION_ASPECT_BATCH_INVALID";return false;}
  }
  std::string extra;if(in>>extra){error="JOURNAL_QUESTION_ASPECT_BATCH_INVALID";return false;}
  for(const auto& item:updates)s.question_aspect_statuses[item.first]=item.second;
  return true;
 }
 if(e.kind=="QUESTION_ASPECT_STATUS_SET"){
  std::istringstream in(e.payload);std::string id,status,extra;
  if(!(in>>std::quoted(id)>>status)||(in>>extra)){error="JOURNAL_QUESTION_ASPECT_STATUS_INVALID";return false;}
  const auto aspect=s.nodes.find(id);
  if(aspect==s.nodes.end()||aspect->second.kind!=NodeKind::QuestionAspect){error="JOURNAL_QUESTION_ASPECT_STATUS_INVALID";return false;}
  QuestionAspectStatus value;if(status=="OPEN")value=QuestionAspectStatus::Open;else if(status=="RESOLVED")value=QuestionAspectStatus::Resolved;else if(status=="DEFERRED")value=QuestionAspectStatus::Deferred;else{error="JOURNAL_QUESTION_ASPECT_STATUS_INVALID";return false;}
  s.question_aspect_statuses[id]=value;return true;
 }
 error="JOURNAL_EVENT_KIND_UNKNOWN";return false;
}
bool ContextGraph::replay(){
 State rebuilt;idempotency_.clear();std::string key;std::uint64_t version=0;bool have_meta=false;
 bool semantic_packet_waiting_for_delta=false;
 for(const auto& e:journal_->committed_events()){
  if(e.kind=="RECOGNITION_PACKET_ADMITTED"){if(have_meta){++version;have_meta=false;}++version;continue;}
  if(e.kind=="SEMANTIC_PACKET_ADMITTED"){
   // A committed semantic packet closes the preceding delta transaction.
   // Without advancing here, the next packet is incorrectly treated as
   // overlapping the still-open DELTA_META from the previous transaction.
   if(have_meta){++version;have_meta=false;}
   if(semantic_packet_waiting_for_delta){last_error_="JOURNAL_SEMANTIC_PACKET_ORDER_INVALID";return false;}
   semantic_packet_waiting_for_delta=true;continue;
  }
  if(e.kind=="RETRIEVAL_TICKET_ISSUED"||e.kind=="RETRIEVAL_TICKET_RESOLVED"){if(have_meta){++version;have_meta=false;}++version;continue;}
  if(e.kind=="TEMPORAL_BOUNDARY_CONTRIBUTION_V1"||e.kind=="TEMPORAL_BOUNDARY_CLEARED_V1"||e.kind=="TEMPORAL_BOUNDARY_CANDIDATE_SELECTED_V1"||e.kind=="TEMPORAL_QUESTION_SNAPSHOT_V1"){if(have_meta){++version;have_meta=false;}++version;continue;}
  if(e.kind=="CHAOS_CLUSTER_SNAPSHOT"){if(have_meta){++version;have_meta=false;}++version;continue;}
  if(e.kind=="RETENTION_MANIFEST_RECORDED"||e.kind=="RETENTION_TOMBSTONE_PROPOSED"){if(!have_meta)++version;continue;}
  if(e.kind=="EVIDENCE_PROVENANCE_SET_V1"){if(!have_meta)++version;continue;}
  if(e.kind=="DELTA_META"){if(have_meta){++version;have_meta=false;}std::istringstream i(e.payload);std::string id,fp;std::uint64_t expected=0;
   if(!(i>>std::quoted(id)>>std::quoted(key)>>expected>>std::quoted(fp))||id.empty()||key.empty()||fp.empty()||expected!=version){last_error_="JOURNAL_DELTA_META_INVALID";return false;}idempotency_[key]=fp;have_meta=true;semantic_packet_waiting_for_delta=false;continue;}
  if(e.kind=="QUESTION_MEMORY_REFERENCED"||e.kind=="QUESTION_MEMORY_REFERENCE_CASCADE"||e.kind=="QUESTION_MEMORY_STATE_SET"||e.kind=="QUESTION_ASPECT_STATUS_SET"||e.kind=="QUESTION_ASPECT_STATUS_BATCH_SET"){if(have_meta){++version;have_meta=false;}if(!replay_event(rebuilt,e,last_error_)){last_error_="JOURNAL_QUESTION_MEMORY_EVENT_INVALID:"+last_error_;return false;}++version;continue;}
  if(e.kind=="CLAIM_VALIDITY_SET"){ClaimValidityRecord record;if(!have_meta||!decode_claim_validity(e.payload,record)||record.recorded_state_version!=version+1){last_error_="JOURNAL_CLAIM_VALIDITY_VERSION_INVALID";return false;}}
  if(e.kind=="EVIDENCE_VALIDITY_SET"){EvidenceValidityRecord record;if(!have_meta||!decode_evidence_validity(e.payload,record)||record.recorded_state_version!=version+1){last_error_="JOURNAL_EVIDENCE_VALIDITY_VERSION_INVALID";return false;}}
  if(e.kind=="TEMPORAL_REASSESSMENT_V1"){
   if(have_meta){++version;have_meta=false;}
   TemporalReassessmentRecord record;
   if(!decode_reassessment(e.payload,record)||record.evaluated_state_version!=version||
      record.recorded_state_version!=version+1||!replay_event(rebuilt,e,last_error_)){
    last_error_="JOURNAL_REASSESSMENT_VERSION_OR_CONTENT_INVALID";return false;
   }
   ++version;continue;
  }
  if(e.kind=="NODE_ADD"||e.kind=="RELATION_ADD"||e.kind=="SCOPE_SPAN_ADD"||e.kind=="FOCUS_SET"||e.kind=="CLAIM_VALIDITY_SET"||e.kind=="EVIDENCE_VALIDITY_SET"||e.kind=="DEPENDENCY_EVALUATION"){if(!have_meta||!replay_event(rebuilt,e,last_error_))return false;}
 }
 if(semantic_packet_waiting_for_delta){last_error_="JOURNAL_SEMANTIC_PACKET_DELTA_MISSING";return false;}
 if(have_meta) { ++version; }
 if(version!=journal_->state_version()){last_error_="GRAPH_REPLAY_VERSION_MISMATCH";return false;}
 state_=std::move(rebuilt);
 return true;
}

bool ContextGraph::prepare_transaction(const SemanticDelta& d, State& staged,
    std::vector<JournalEvent>& events, std::string& error) const {
 events.clear(); error.clear();
 if(d.delta_id.empty()||d.idempotency_key.empty()||
    (d.nodes.empty()&&d.relations.empty()&&d.scope_spans.empty()&&d.claim_validity_updates.empty()&&d.evidence_validity_updates.empty()&&!d.focus)){
  error="DELTA_EMPTY_OR_ID_INVALID";return false;
 }
 const auto fp=fingerprint(d);
 const auto old=idempotency_.find(d.idempotency_key);
 if(old!=idempotency_.end()){
  if(old->second==fp){staged=state_;return true;}
  error="IDEMPOTENCY_KEY_REUSED";return false;
 }
 if(d.expected_state_version!=state_version()){error="STATE_VERSION_CONFLICT";return false;}
 staged=state_;
 for(const auto& n:d.nodes)if(!apply_node(staged,n.node,error))return false;
 for(const auto& r:d.relations)if(!apply_relation(staged,r.relation,error))return false;
 for(const auto& scope:d.scope_spans)if(!apply_scope_span(staged,scope,error))return false;
 for(const auto& validity:d.claim_validity_updates)if(!apply_claim_validity(staged,validity,d.expected_state_version+1,error))return false;
 for(const auto& validity:d.evidence_validity_updates)if(!apply_evidence_validity(staged,validity,d.expected_state_version+1,error))return false;
 if(d.focus&&!apply_focus(staged,d.focus->focus,error))return false;

 std::set<std::string> changed;
 auto add_if_node=[&](const std::string& id){if(staged.nodes.count(id))changed.insert(id);};
 for(const auto& n:d.nodes){
  add_if_node(n.node.id);
  // A newly introduced dependency must be evaluated against its current prerequisite.
  if(n.node.kind==NodeKind::Dependency&&!n.node.references.empty())
   add_if_node(n.node.references[0]);
 }
 for(const auto& r:d.relations){add_if_node(r.relation.from);add_if_node(r.relation.to);}
 for(const auto& validity:d.claim_validity_updates){add_if_node(validity.claim_id);add_if_node(validity.basis_evidence_id);}
 for(const auto& validity:d.evidence_validity_updates){add_if_node(validity.evidence_id);add_if_node(validity.basis_evidence_id);}
 for(const auto& scope:d.scope_spans){
  for(const auto& id:scope.span.target_refs)add_if_node(id);
  for(const auto& id:scope.span.antecedent_refs)add_if_node(id);
 }
 if(d.focus){add_if_node(d.focus->focus.cluster_id);add_if_node(d.focus->focus.interest_id);}
 bool has_dependencies=false;
 for(const auto& item:staged.nodes)if(item.second.kind==NodeKind::Dependency){has_dependencies=true;break;}
 const auto fp_value=fingerprint(d);
 events=encode(d,fp_value);
 if(has_dependencies&&!changed.empty()){
  constexpr std::size_t kMaximumDependencyImpactedNodes = 4096;
  constexpr std::size_t kMaximumDependencyPredicateOperations = 65536;
  std::map<std::string, ClaimValidityRecord> latest_validity;
  for (const auto& item : staged.claim_validity_history) {
   if (!item.second.empty()) latest_validity.emplace(item.first, item.second.back());
  }
  const auto evaluation=DependencyEngine::evaluate(staged.nodes,
      std::vector<std::string>(changed.begin(),changed.end()),
      kMaximumDependencyImpactedNodes, kMaximumDependencyPredicateOperations,
      latest_validity);
  if(!evaluation.complete){error=evaluation.error.empty()?"DEPENDENCY_EVALUATION_FAILED":evaluation.error;events.clear();return false;}
  DependencyEvaluationRecord record;record.delta_id=d.delta_id;
  record.impacted_nodes=evaluation.impacted_nodes;
  record.rejected_dependencies=evaluation.rejected_dependencies;
  for(const auto& id:evaluation.triggered_dependencies){
   const auto dep_it=staged.nodes.find(id);
   if(dep_it==staged.nodes.end()||dep_it->second.kind!=NodeKind::Dependency||
      dep_it->second.references.size()!=2){
    error="DEPENDENCY_TRIGGER_RECORD_INVALID";events.clear();return false;
   }
   const auto& dep=dep_it->second;
   record.triggered.push_back({id,dep.references[0],dep.references[1]});
  }
  staged.dependency_evaluations[d.delta_id]=record;
  events.push_back({d.delta_id+"/dependency-evaluation","DEPENDENCY_EVALUATION",
      encode_dependency_evaluation(record)});
 }
 return true;
}

bool ContextGraph::prepare_events(const SemanticDelta& d,std::vector<JournalEvent>& events,std::string& error)const{
 State staged;
 return prepare_transaction(d,staged,events,error);
}

bool ContextGraph::append_cluster_membership(SemanticDelta& delta,
    const std::string& parent_cluster_id, const std::string& classification,
    const std::string& partition_key, const std::string& member_node_id,
    const std::string& relation_prefix, std::string& assigned_cluster_id,
    std::string& error) const {
 if(parent_cluster_id.empty()||classification.empty()||partition_key.empty()||
    member_node_id.empty()||relation_prefix.empty()||
    classification.size()>128||partition_key.size()>1024||member_node_id.size()>1024){
  error="CLUSTER_CLASSIFICATION_INPUT_INVALID";return false;
 }
 const auto parent=state_.nodes.find(parent_cluster_id);
 bool parent_in_delta=false;
 for(const auto& item:delta.nodes)
  if(item.node.id==parent_cluster_id&&item.node.kind==NodeKind::Cluster)parent_in_delta=true;
 if((parent==state_.nodes.end()&&!parent_in_delta)||
    (parent!=state_.nodes.end()&&parent->second.kind!=NodeKind::Cluster)){
  error="CLUSTER_CLASSIFICATION_PARENT_NOT_FOUND";return false;
 }
 const std::string digest=sha256_hex(classification+"|"+partition_key);
 assigned_cluster_id=parent_cluster_id+"/classified/"+digest.substr(0,24);
 const auto existing=state_.nodes.find(assigned_cluster_id);
 bool cluster_in_delta=false;
 for(const auto& item:delta.nodes)
  if(item.node.id==assigned_cluster_id&&item.node.kind==NodeKind::Cluster)cluster_in_delta=true;
 if(existing==state_.nodes.end()&&!cluster_in_delta){
  delta.nodes.push_back({{assigned_cluster_id,NodeKind::Cluster,
      "CLASS="+classification+";PARTITION="+partition_key, {}}});
 }
 const std::string parent_relation=relation_prefix+"/parent/"+digest.substr(0,24);
 bool parent_edge_exists=state_.relations.count(parent_relation)!=0;
 for(const auto& item:delta.relations)
  if(item.relation.id==parent_relation)parent_edge_exists=true;
 if(!parent_edge_exists)
  delta.relations.push_back({{parent_relation,RelationKind::Contains,
      parent_cluster_id,assigned_cluster_id}});
 const std::string member_digest=sha256_hex(assigned_cluster_id+"|"+member_node_id);
 const std::string member_relation=relation_prefix+"/member/"+member_digest.substr(0,24);
 bool member_edge_exists=state_.relations.count(member_relation)!=0;
 for(const auto& item:delta.relations)
  if(item.relation.id==member_relation)member_edge_exists=true;
 if(!member_edge_exists)
  delta.relations.push_back({{member_relation,RelationKind::Contains,
      assigned_cluster_id,member_node_id}});
 return true;
}

DeltaResult ContextGraph::apply(const SemanticDelta& d){
 State staged;std::vector<JournalEvent> events;std::string error;
 if(!prepare_transaction(d,staged,events,error))
  return {false,false,state_version(),error};
 if(events.empty())return {true,true,state_version(),{}};
 const auto committed=journal_->commit(d.delta_id,d.idempotency_key,d.expected_state_version,events);
 if(!committed.committed)return {false,false,state_version(),committed.error};
 state_=std::move(staged);idempotency_[d.idempotency_key]=fingerprint(d);
 return {true,committed.duplicate,state_version(),{}};
}

ClaimTemporalState evaluate_claim_validity(const ClaimValidityRecord& record,const std::string& instant){
 ParsedRfc3339 at{},from{},until{};
 if(!parse_rfc3339(instant,at)||!parse_rfc3339(record.validity.valid_from,from))return ClaimTemporalState::Unknown;
 if(time_less(at,from))return ClaimTemporalState::NotYetValid;
 if(!record.validity.valid_until.empty()){
  if(!parse_rfc3339(record.validity.valid_until,until))return ClaimTemporalState::Unknown;
  if(!time_less(at,until))return ClaimTemporalState::Expired;
 }
 return ClaimTemporalState::Valid;
}
ClaimTemporalState ContextGraph::claim_validity_at(const std::string& id,const std::string& instant)const{
 const auto latest=latest_claim_validity(id);
 return latest?evaluate_claim_validity(*latest,instant):ClaimTemporalState::Unknown;
}
ClaimTemporalState ContextGraph::evidence_validity_at(const std::string& id,const std::string& instant)const{
 const auto latest=latest_evidence_validity(id);
 ClaimValidityRecord compatible; if(latest){compatible.validity.valid_from=latest->validity.valid_from;compatible.validity.valid_until=latest->validity.valid_until;} return latest?evaluate_claim_validity(compatible,instant):ClaimTemporalState::Unknown;
}

QuestionAspectStatus ContextGraph::question_aspect_status(const std::string& id) const {
  const auto found = state_.question_aspect_statuses.find(id);
  return found == state_.question_aspect_statuses.end()
      ? QuestionAspectStatus::Open : found->second;
}

bool ContextGraph::set_question_aspect_status(const std::string& id,
    QuestionAspectStatus status, std::uint64_t expected, std::string& error) {
  error.clear();
  const auto aspect = state_.nodes.find(id);
  if (aspect == state_.nodes.end() || aspect->second.kind != NodeKind::QuestionAspect) {
    error = "QUESTION_ASPECT_NOT_FOUND"; return false;
  }
  if (expected != state_version()) { error = "STATE_VERSION_CONFLICT"; return false; }
  if (question_aspect_status(id) == status) return true;
  const char* name = status == QuestionAspectStatus::Open ? "OPEN" :
      status == QuestionAspectStatus::Resolved ? "RESOLVED" : "DEFERRED";
  std::ostringstream payload; payload << std::quoted(id) << ' ' << name;
  const auto key = "question-aspect/status/" + id + "/" + std::to_string(expected + 1);
  const auto committed = journal_->commit(key, key, expected,
      {{key + "/event", "QUESTION_ASPECT_STATUS_SET", payload.str()}});
  if (!committed.committed) { error = committed.error; return false; }
  state_.question_aspect_statuses[id] = status;
  return true;
}


bool ContextGraph::apply_question_aspect_statuses(
    const std::vector<std::pair<std::string, QuestionAspectStatus>>& updates,
    std::uint64_t expected, std::string& error) {
  error.clear();
  if (updates.empty()) { error = "QUESTION_ASPECT_BATCH_EMPTY"; return false; }
  if (expected != state_version()) { error = "STATE_VERSION_CONFLICT"; return false; }
  std::map<std::string, QuestionAspectStatus> unique;
  for (const auto& update : updates) {
    const auto& id = update.first;
    const auto found = state_.nodes.find(id);
    if (id.empty() || found == state_.nodes.end() || found->second.kind != NodeKind::QuestionAspect) {
      error = "QUESTION_ASPECT_NOT_FOUND"; return false;
    }
    if (!unique.emplace(id, update.second).second) { error = "QUESTION_ASPECT_DUPLICATE"; return false; }
    for (const auto& ref : found->second.references) {
      const auto q = state_.nodes.find(ref);
      if (q != state_.nodes.end() && q->second.kind == NodeKind::Question &&
          question_memory_state(ref) == QuestionMemoryState::Archived) {
        error = "QUESTION_ARCHIVED"; return false;
      }
    }
  }
  for (const auto& update : unique) {
    if (update.second != QuestionAspectStatus::Resolved ||
        question_aspect_status(update.first) == QuestionAspectStatus::Resolved) continue;
    const auto& aspect = state_.nodes.at(update.first);
    std::set<std::string> anchors{update.first}, clusters, claims;
    for (const auto& ref : aspect.references) {
      const auto q = state_.nodes.find(ref);
      if (q != state_.nodes.end() && q->second.kind == NodeKind::Question) anchors.insert(ref);
    }
    for (const auto& rel : state_.relations) {
      const auto& edge = rel.second;
      if (edge.kind == RelationKind::About && anchors.count(edge.from)) {
        const auto target = state_.nodes.find(edge.to);
        if (target != state_.nodes.end() && (target->second.kind == NodeKind::Cluster || target->second.kind == NodeKind::Entity))
          clusters.insert(edge.to);
      }
    }
    for (const auto& rel : state_.relations) {
      const auto& edge = rel.second;
      if (edge.kind == RelationKind::About && clusters.count(edge.to)) {
        const auto source = state_.nodes.find(edge.from);
        if (source != state_.nodes.end() && source->second.kind == NodeKind::Claim) claims.insert(edge.from);
      }
    }
    bool supported = false, contradicted = false, retracted_support = false;
    for (const auto& rel : state_.relations) {
      const auto& edge = rel.second;
      if (edge.kind == RelationKind::Supports) {
        const auto from = state_.nodes.find(edge.from), to = state_.nodes.find(edge.to);
        std::string claim_id;
        if (from != state_.nodes.end() && to != state_.nodes.end() &&
            from->second.kind == NodeKind::Evidence && to->second.kind == NodeKind::Claim) claim_id = edge.to;
        else if (from != state_.nodes.end() && to != state_.nodes.end() &&
            from->second.kind == NodeKind::Claim && to->second.kind == NodeKind::Evidence) claim_id = edge.from;
        if (claims.count(claim_id) && claim_status(claim_id) == ClaimStatus::Active) supported = true;
      }
      if (edge.kind == RelationKind::Contradicts || edge.kind == RelationKind::ConflictsWith ||
          edge.kind == RelationKind::Retracts) {
        if (claims.count(edge.from) || claims.count(edge.to)) {
          contradicted = true;
          if (edge.kind == RelationKind::Retracts) retracted_support = true;
        }
      }
    }
    if (!supported || contradicted) {
      error = retracted_support ? "QUESTION_REEVALUATION_EVIDENCE_RETRACTED" :
          contradicted ? "QUESTION_REEVALUATION_CONFLICT_REMAINS" : "QUESTION_REEVALUATION_SUPPORT_MISSING";
      return false;
    }
  }
  std::vector<std::pair<std::string, QuestionAspectStatus>> changed;
  for (const auto& item : unique)
    if (question_aspect_status(item.first) != item.second) changed.push_back(item);
  if (changed.empty()) return true;
  std::ostringstream payload; payload << changed.size();
  for (const auto& item : changed) {
    const char* status = item.second == QuestionAspectStatus::Open ? "OPEN" :
        item.second == QuestionAspectStatus::Resolved ? "RESOLVED" : "DEFERRED";
    payload << ' ' << std::quoted(item.first) << ' ' << status;
  }
  const auto key = "question-aspect/batch/" + std::to_string(expected + 1);
  const auto committed = journal_->commit(key, key, expected,
      {{key + "/event", "QUESTION_ASPECT_STATUS_BATCH_SET", payload.str()}});
  if (!committed.committed) { error = committed.error; return false; }
  for (const auto& item : changed) state_.question_aspect_statuses[item.first] = item.second;
  return true;
}

QuestionMemoryState ContextGraph::question_memory_state(const std::string& id) const {const auto it=state_.question_memory_states.find(id);return it==state_.question_memory_states.end()?QuestionMemoryState::Active:it->second;}
std::uint64_t ContextGraph::question_reference_count(const std::string& id) const {const auto it=state_.question_reference_counts.find(id);return it==state_.question_reference_counts.end()?0:it->second;}
bool ContextGraph::record_question_reference(const std::string& id,std::uint64_t expected,bool& reactivated,std::string& error){
 error.clear();reactivated=false;const auto q=state_.nodes.find(id);if(q==state_.nodes.end()||q->second.kind!=NodeKind::Question){error="QUESTION_NOT_FOUND";return false;}if(expected!=state_version()){error="STATE_VERSION_CONFLICT";return false;}if(question_memory_state(id)==QuestionMemoryState::Archived){error="QUESTION_ARCHIVED";return false;}const auto next=question_reference_count(id)+1;std::ostringstream payload;payload<<std::quoted(id)<<' '<<next;const auto key="question-memory/reference/"+id+"/"+std::to_string(next);const auto was_dormant=question_memory_state(id)==QuestionMemoryState::Dormant;const auto result=journal_->commit(key,key,expected,{{key+"/event","QUESTION_MEMORY_REFERENCED",payload.str()}});if(!result.committed){error=result.error;return false;}state_.question_reference_counts[id]=next;if(was_dormant)state_.question_memory_states[id]=QuestionMemoryState::Active;reactivated=was_dormant;return true;
}
bool ContextGraph::record_question_reference_with_dependents(const std::string& id,std::uint64_t expected,std::size_t max_depth,std::size_t max_nodes,bool& reactivated,std::size_t& reactivated_dependents,std::string& error){
 error.clear();reactivated=false;reactivated_dependents=0;
 if(max_depth==0||max_depth>64){error="QUESTION_REACTIVATION_DEPTH_INVALID";return false;}
 if(max_nodes==0||max_nodes>4096){error="QUESTION_REACTIVATION_NODE_LIMIT_INVALID";return false;}
 if(expected!=state_version()){error="STATE_VERSION_CONFLICT";return false;}
 const auto root=state_.nodes.find(id);if(root==state_.nodes.end()||root->second.kind!=NodeKind::Question){error="QUESTION_NOT_FOUND";return false;}
 if(question_memory_state(id)==QuestionMemoryState::Archived){error="QUESTION_ARCHIVED";return false;}
 struct Edge{std::string dependent,relation;};
 std::map<std::string,std::vector<Edge>> reverse;
 for(const auto& item:state_.relations){const auto& r=item.second;if(r.kind!=RelationKind::DependsOn)continue;const auto from=state_.nodes.find(r.from),to=state_.nodes.find(r.to);if(from==state_.nodes.end()||to==state_.nodes.end()||from->second.kind!=NodeKind::Question||to->second.kind!=NodeKind::Question)continue;reverse[r.to].push_back({r.from,r.id});}
 for(auto& item:reverse)std::sort(item.second.begin(),item.second.end(),[](const Edge&a,const Edge&b){return a.dependent<b.dependent||(a.dependent==b.dependent&&a.relation<b.relation);});
 std::vector<std::pair<std::string,std::size_t>> queue{{id,0}};std::set<std::string> visited{id};std::vector<std::string> dormant;
 for(std::size_t cursor=0;cursor<queue.size();++cursor){const auto current=queue[cursor];if(current.second>=max_depth)continue;const auto edges=reverse.find(current.first);if(edges==reverse.end())continue;for(const auto& edge:edges->second){if(!visited.insert(edge.dependent).second)continue;if(visited.size()-1>max_nodes){error="QUESTION_REACTIVATION_NODE_LIMIT";return false;}queue.push_back({edge.dependent,current.second+1});if(question_memory_state(edge.dependent)==QuestionMemoryState::Dormant)dormant.push_back(edge.dependent);}}
 const auto next=question_reference_count(id)+1;std::ostringstream payload;payload<<std::quoted(id)<<' '<<next<<' '<<dormant.size();for(const auto& dependent:dormant)payload<<' '<<std::quoted(dependent);const auto key="question-memory/reference-cascade/"+id+"/"+std::to_string(next);std::vector<JournalEvent> events{{key+"/event","QUESTION_MEMORY_REFERENCE_CASCADE",payload.str()}};
 const bool was_dormant=question_memory_state(id)==QuestionMemoryState::Dormant;const auto result=journal_->commit(key,key,expected,events);if(!result.committed){error=result.error;return false;}
 state_.question_reference_counts[id]=next;if(was_dormant)state_.question_memory_states[id]=QuestionMemoryState::Active;for(const auto& dependent:dormant)state_.question_memory_states[dependent]=QuestionMemoryState::Active;
 reactivated=was_dormant;reactivated_dependents=dormant.size();return true;
}
bool ContextGraph::set_question_memory_state(const std::string& id,QuestionMemoryState v,std::uint64_t expected,std::string& error){
 error.clear();const auto q=state_.nodes.find(id);if(q==state_.nodes.end()||q->second.kind!=NodeKind::Question){error="QUESTION_NOT_FOUND";return false;}if(expected!=state_version()){error="STATE_VERSION_CONFLICT";return false;}if(question_memory_state(id)==v)return true;
 const char* name=v==QuestionMemoryState::Active?"ACTIVE":v==QuestionMemoryState::Dormant?"DORMANT":"ARCHIVED";std::ostringstream payload;payload<<std::quoted(id)<<' '<<name;const auto key="question-memory/state/"+id+"/"+std::to_string(expected+1);const auto result=journal_->commit(key,key,expected,{{key+"/event","QUESTION_MEMORY_STATE_SET",payload.str()}});if(!result.committed){error=result.error;return false;}state_.question_memory_states[id]=v;return true;
}
bool ContextGraph::enforce_question_memory_capacity(const std::vector<std::pair<std::string,double>>& scores,std::size_t max_active,std::uint64_t expected,std::vector<std::string>& demoted,std::string& error){
 error.clear();demoted.clear();if(expected!=state_version()){error="STATE_VERSION_CONFLICT";return false;}if(scores.size()>4096||max_active>4096){error="QUESTION_MEMORY_CAPACITY_LIMIT";return false;}
 std::map<std::string,double> supplied;for(const auto& item:scores){if(item.first.empty()||!std::isfinite(item.second)||item.second<0||item.second>1){error="QUESTION_MEMORY_SCORE_INVALID";return false;}if(!supplied.emplace(item.first,item.second).second){error="QUESTION_MEMORY_DUPLICATE_SCORE";return false;}auto n=state_.nodes.find(item.first);if(n==state_.nodes.end()||n->second.kind!=NodeKind::Question){error="QUESTION_NOT_FOUND";return false;}}
 std::vector<std::pair<std::string,double>> active;for(const auto& n:state_.nodes){if(n.second.kind!=NodeKind::Question||question_memory_state(n.first)!=QuestionMemoryState::Active)continue;auto v=supplied.find(n.first);if(v==supplied.end()){error="QUESTION_MEMORY_SCORE_MISSING";return false;}active.emplace_back(n.first,v->second);}
 if(active.size()!=supplied.size()){error="QUESTION_MEMORY_SCORE_NOT_ACTIVE";return false;}
 std::sort(active.begin(),active.end(),[](const auto&a,const auto&b){return a.second!=b.second?a.second>b.second:a.first<b.first;});
 if(active.size()<=max_active)return true;
 for(std::size_t i=max_active;i<active.size();++i)demoted.push_back(active[i].first);
 const std::string tx="question-memory/capacity/"+std::to_string(expected);std::vector<JournalEvent> events;events.reserve(demoted.size());
 for(const auto&id:demoted){std::ostringstream p;p<<std::quoted(id)<<" DORMANT";events.push_back({tx+"/demote/"+id,"QUESTION_MEMORY_STATE_SET",p.str()});}
 const auto result=journal_->commit(tx,tx,expected,events);if(!result.committed){error=result.error;demoted.clear();return false;}
 for(const auto&id:demoted) { state_.question_memory_states[id]=QuestionMemoryState::Dormant; }
 return true;
}

class PreparedContextGraphSnapshotRestore final : public PreparedSnapshotRestore {
 public:
  PreparedContextGraphSnapshotRestore(ContextGraph* owner, ContextGraph::State state,
      std::map<std::string, std::string> idempotency)
      : owner_(owner), state_(std::move(state)), idempotency_(std::move(idempotency)) {}
  void commit() noexcept override {
    owner_->state_ = std::move(state_);
    owner_->idempotency_.swap(idempotency_);
  }
 private:
  ContextGraph* owner_;
  ContextGraph::State state_;
  std::map<std::string, std::string> idempotency_;
};

std::string ContextGraphSnapshotDomain::domain_id() const {
  return "context_graph";
}
std::string ContextGraphSnapshotDomain::schema_id() const {
  return "surge.context-graph.native-v1";
}
bool ContextGraphSnapshotDomain::capture(std::string* payload,
                                         std::string* error) const {
  if (!payload) { if (error) *error = "SNAPSHOT_OUTPUT_REQUIRED"; return false; }
  const auto& s = graph_.state_;
  std::ostringstream out;
  out << "CGS1 " << s.nodes.size() << ' ';
  for (const auto& item : s.nodes)
    out << std::quoted(encode_node(item.second)) << ' ';
  out << s.relations.size() << ' ';
  for (const auto& item : s.relations)
    out << std::quoted(encode_relation(item.second)) << ' ';
  out << s.scope_spans.size() << ' ';
  for (const auto& item : s.scope_spans) {
    const auto& record = item.second;
    out << std::quoted(encode_scope(AddScopeSpan{record.proposal, {}})) << ' '
        << static_cast<int>(record.status) << ' '
        << static_cast<int>(record.effect) << ' '
        << (record.may_emit_unconditional_claim ? 1 : 0) << ' ';
  }
  out << s.focus_history.size() << ' ';
  for (const auto& focus : s.focus_history)
    out << std::quoted(encode_focus(focus)) << ' ';
  out << (s.focus ? 1 : 0) << ' ';
  if (s.focus) out << std::quoted(encode_focus(*s.focus)) << ' ';

  auto write_enum_map = [&out](const auto& values) {
    out << values.size() << ' ';
    for (const auto& item : values)
      out << std::quoted(item.first) << ' '
          << static_cast<int>(item.second) << ' ';
  };
  write_enum_map(s.question_aspect_statuses);
  write_enum_map(s.question_memory_states);
  out << s.question_reference_counts.size() << ' ';
  for (const auto& item : s.question_reference_counts)
    out << std::quoted(item.first) << ' ' << item.second << ' ';
  write_enum_map(s.claim_statuses);

  out << s.claim_validity_history.size() << ' ';
  for (const auto& item : s.claim_validity_history) {
    out << std::quoted(item.first) << ' ' << item.second.size() << ' ';
    for (const auto& record : item.second)
      out << std::quoted(encode_claim_validity(record)) << ' ';
  }
  out << s.evidence_validity_history.size() << ' ';
  for (const auto& item : s.evidence_validity_history) {
    out << std::quoted(item.first) << ' ' << item.second.size() << ' ';
    for (const auto& record : item.second)
      out << std::quoted(encode_evidence_validity(record)) << ' ';
  }
  out << s.dependency_evaluations.size() << ' ';
  for (const auto& item : s.dependency_evaluations)
    out << std::quoted(item.first) << ' '
        << std::quoted(encode_dependency_evaluation(item.second)) << ' ';
  out << s.reassessment_history.size() << ' ';
  for (const auto& item : s.reassessment_history) {
    out << std::quoted(item.first) << ' ' << item.second.size() << ' ';
    for (const auto& record : item.second)
      out << std::quoted(reassessment_event_payload(record)) << ' ';
  }
  out << graph_.idempotency_.size() << ' ';
  for (const auto& item : graph_.idempotency_)
    out << std::quoted(item.first) << ' ' << std::quoted(item.second) << ' ';
  *payload = out.str();
  if (error) error->clear();
  return true;
}

std::unique_ptr<PreparedSnapshotRestore>
ContextGraphSnapshotDomain::prepare_restore(const std::string& payload,
                                             std::string* error) {
  return prepare_restore_with_tail(payload, {}, error);
}

std::unique_ptr<PreparedSnapshotRestore>
ContextGraphSnapshotDomain::prepare_restore_with_tail(
    const std::string& payload, const std::vector<JournalEvent>& tail,
    std::string* error) {
  std::istringstream in(payload);
  std::string magic, extra;
  std::size_t count = 0;
  ContextGraph::State candidate;
  std::map<std::string, std::string> idempotency;
  auto fail_restore = [error](const char* code)
      -> std::unique_ptr<PreparedSnapshotRestore> {
    if (error) *error = code;
    return nullptr;
  };
  if (!(in >> magic >> count) || magic != "CGS1" || count > 100000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_HEADER_INVALID");
  std::string detail;
  std::map<std::string, GraphNode> pending_nodes;
  for (std::size_t i = 0; i < count; ++i) {
    std::string encoded;
    GraphNode node;
    if (!(in >> std::quoted(encoded)) || !decode_node(encoded, node) ||
        node.id.empty() || !pending_nodes.emplace(node.id, node).second)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_NODE_INVALID");
  }
  // Node references may point forward in canonical ID order. Rebuild in
  // dependency order rather than assuming lexical order is topological.
  while (!pending_nodes.empty()) {
    bool progressed = false;
    for (auto it = pending_nodes.begin(); it != pending_nodes.end();) {
      const bool references_ready = std::all_of(
          it->second.references.begin(), it->second.references.end(),
          [&candidate](const std::string& ref) { return candidate.nodes.count(ref) != 0; });
      if (!references_ready) { ++it; continue; }
      if (!graph_.apply_node(candidate, it->second, detail))
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_NODE_INVALID");
      it = pending_nodes.erase(it);
      progressed = true;
    }
    if (!progressed)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_NODE_REFERENCE_CYCLE_OR_MISSING");
  }
  if (!(in >> count) || count > 1000000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_RELATION_COUNT_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string encoded;
    GraphRelation relation;
    if (!(in >> std::quoted(encoded)) || !decode_relation(encoded, relation) ||
        !graph_.apply_relation(candidate, relation, detail))
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_RELATION_INVALID");
  }
  if (!(in >> count) || count > 1000000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_SCOPE_COUNT_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string encoded;
    int status = 0, effect = 0, may_emit = 0;
    AddScopeSpan add;
    if (!(in >> std::quoted(encoded) >> status >> effect >> may_emit) ||
        !decode_scope(encoded, add) || add.span.span_id.empty() ||
        status < 0 || status > 1 || effect < 0 || effect > 9 ||
        (may_emit != 0 && may_emit != 1) ||
        add.span.source_digest.empty() || add.span.offset_basis.empty() ||
        add.span.opener.source_digest != add.span.source_digest ||
        add.span.opener.offset_basis != add.span.offset_basis ||
        add.span.opener.start_offset > add.span.opener.end_offset)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_SCOPE_INVALID");
    if (add.span.closer &&
        (add.span.closer->source_digest != add.span.source_digest ||
         add.span.closer->offset_basis != add.span.offset_basis ||
         add.span.closer->start_offset > add.span.closer->end_offset))
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_SCOPE_ANCHOR_INVALID");
    for (const auto& ref : add.span.target_refs)
      if (!candidate.nodes.count(ref))
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_SCOPE_TARGET_INVALID");
    for (const auto& ref : add.span.antecedent_refs)
      if (!candidate.nodes.count(ref))
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_SCOPE_ANTECEDENT_INVALID");
    ScopeSpanRecord record{add.span, static_cast<ScopeStatus>(status),
        static_cast<ScopeEffect>(effect), may_emit != 0};
    if (!candidate.scope_spans.emplace(add.span.span_id, std::move(record)).second)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_SCOPE_DUPLICATE");
  }

  if (!(in >> count) || count > 1000000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_FOCUS_HISTORY_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string encoded;
    FocusRecord focus;
    if (!(in >> std::quoted(encoded)) || !decode_focus(encoded, focus) ||
        !graph_.apply_focus(candidate, focus, detail))
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_FOCUS_INVALID");
  }
  int has_focus = 0;
  if (!(in >> has_focus) || (has_focus != 0 && has_focus != 1))
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_FOCUS_MARKER_INVALID");
  if (has_focus) {
    std::string encoded;
    FocusRecord focus;
    if (!(in >> std::quoted(encoded)) || !decode_focus(encoded, focus) ||
        !candidate.focus || candidate.focus->cluster_id != focus.cluster_id ||
        candidate.focus->interest_id != focus.interest_id ||
        candidate.focus->chaos_cluster != focus.chaos_cluster)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_FOCUS_MISMATCH");
  } else if (candidate.focus || !candidate.focus_history.empty()) {
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_FOCUS_MISSING");
  }

  auto read_enum_map = [&in](auto& values, int maximum,
                             const std::map<std::string, GraphNode>& nodes,
                             NodeKind required_kind) {
    std::size_t n = 0;
    if (!(in >> n) || n > 1000000) return false;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < n; ++i) {
      std::string id;
      int value = 0;
      if (!(in >> std::quoted(id) >> value) || id.empty() ||
          value < 0 || value > maximum || !seen.insert(id).second) return false;
      const auto node = nodes.find(id);
      if (node == nodes.end() || node->second.kind != required_kind)
        return false;
      values[id] = static_cast<typename std::decay_t<decltype(values)>::mapped_type>(value);
    }
    return true;
  };
  if (!read_enum_map(candidate.question_aspect_statuses, 2, candidate.nodes,
                     NodeKind::QuestionAspect) ||
      !read_enum_map(candidate.question_memory_states, 2, candidate.nodes,
                     NodeKind::Question))
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_QUESTION_STATE_INVALID");

  if (!(in >> count) || count > 1000000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_QUESTION_REFERENCE_COUNT_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string id;
    std::uint64_t references = 0;
    if (!(in >> std::quoted(id) >> references) || id.empty() || references == 0 ||
        !candidate.nodes.count(id) ||
        candidate.nodes.at(id).kind != NodeKind::Question ||
        !candidate.question_reference_counts.emplace(id, references).second)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_QUESTION_REFERENCE_INVALID");
  }
  if (!read_enum_map(candidate.claim_statuses, 2, candidate.nodes, NodeKind::Claim))
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_CLAIM_STATUS_INVALID");
  std::size_t claim_node_count = 0;
  for (const auto& node : candidate.nodes)
    if (node.second.kind == NodeKind::Claim) ++claim_node_count;
  if (candidate.claim_statuses.size() != claim_node_count)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_CLAIM_STATUS_INCOMPLETE");

  if (!(in >> count) || count > 100000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_CLAIM_VALIDITY_COUNT_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string id;
    std::size_t records = 0;
    if (!(in >> std::quoted(id) >> records) || id.empty() ||
        records > 100000) return fail_restore("SNAPSHOT_CONTEXT_GRAPH_CLAIM_VALIDITY_INVALID");
    for (std::size_t j = 0; j < records; ++j) {
      std::string encoded;
      ClaimValidityRecord record;
      if (!(in >> std::quoted(encoded)) || !decode_claim_validity(encoded, record) ||
          record.validity.claim_id != id ||
          !graph_.apply_claim_validity(candidate, record.validity,
                                       record.recorded_state_version, detail))
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_CLAIM_VALIDITY_INVALID");
    }
  }
  if (!(in >> count) || count > 100000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_EVIDENCE_VALIDITY_COUNT_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string id;
    std::size_t records = 0;
    if (!(in >> std::quoted(id) >> records) || id.empty() ||
        records > 100000) return fail_restore("SNAPSHOT_CONTEXT_GRAPH_EVIDENCE_VALIDITY_INVALID");
    for (std::size_t j = 0; j < records; ++j) {
      std::string encoded;
      EvidenceValidityRecord record;
      if (!(in >> std::quoted(encoded)) || !decode_evidence_validity(encoded, record) ||
          record.validity.evidence_id != id ||
          !graph_.apply_evidence_validity(candidate, record.validity,
                                          record.recorded_state_version, detail))
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_EVIDENCE_VALIDITY_INVALID");
    }
  }
  if (!(in >> count) || count > 1000000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_DEPENDENCY_EVALUATION_COUNT_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string id, encoded;
    if (!(in >> std::quoted(id) >> std::quoted(encoded)) || id.empty() ||
        !graph_.replay_event(candidate,
            JournalEvent{"snapshot/dependency/" + id, "DEPENDENCY_EVALUATION", encoded},
            detail) || !candidate.dependency_evaluations.count(id))
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_DEPENDENCY_EVALUATION_INVALID");
  }
  if (!(in >> count) || count > 100000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_REASSESSMENT_COUNT_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string id;
    std::size_t records = 0;
    if (!(in >> std::quoted(id) >> records) || id.empty() ||
        records > 100000) return fail_restore("SNAPSHOT_CONTEXT_GRAPH_REASSESSMENT_INVALID");
    for (std::size_t j = 0; j < records; ++j) {
      std::string encoded;
      TemporalReassessmentRecord record;
      if (!(in >> std::quoted(encoded)) || !decode_reassessment(encoded, record) ||
          record.claim_id != id || !graph_.apply_reassessment(candidate, record, detail))
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_REASSESSMENT_INVALID");
    }
  }
  if (!(in >> count) || count > 1000000)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_IDEMPOTENCY_COUNT_INVALID");
  for (std::size_t i = 0; i < count; ++i) {
    std::string key, fingerprint;
    if (!(in >> std::quoted(key) >> std::quoted(fingerprint)) ||
        key.empty() || fingerprint.empty() ||
        !idempotency.emplace(key, fingerprint).second)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_IDEMPOTENCY_INVALID");
  }
  if (in >> extra) return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TRAILING_DATA");

  bool have_meta = false;
  bool semantic_packet_waiting_for_delta = false;
  std::uint64_t transaction_version = 0;
  auto close_meta = [&]() { have_meta = false; };
  for (const auto& event : tail) {
    if (event.state_version == 0) return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_WATERMARK_INVALID");
    if (transaction_version != 0 && event.state_version < transaction_version)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_ORDER_INVALID");
    if (transaction_version != 0 && event.state_version > transaction_version && have_meta)
      close_meta();
    transaction_version = event.state_version;

    if (event.kind == "DELTA_META") {
      if (have_meta) close_meta();
      std::istringstream meta(event.payload);
      std::string delta_id, key, fingerprint, trailing;
      std::uint64_t expected = 0;
      if (!(meta >> std::quoted(delta_id) >> std::quoted(key) >> expected >>
            std::quoted(fingerprint)) || (meta >> trailing) ||
          delta_id.empty() || key.empty() || fingerprint.empty() ||
          event.state_version == 0 || expected + 1 != event.state_version ||
          !idempotency.emplace(key, fingerprint).second)
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_DELTA_META_INVALID");
      have_meta = true;
      semantic_packet_waiting_for_delta = false;
      continue;
    }
    if (event.kind == "SEMANTIC_PACKET_ADMITTED") {
      if (semantic_packet_waiting_for_delta)
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_PACKET_ORDER_INVALID");
      semantic_packet_waiting_for_delta = true;
      continue;
    }

    const bool question_memory =
        event.kind == "QUESTION_MEMORY_REFERENCED" ||
        event.kind == "QUESTION_MEMORY_REFERENCE_CASCADE" ||
        event.kind == "QUESTION_MEMORY_STATE_SET" ||
        event.kind == "QUESTION_ASPECT_STATUS_SET" ||
        event.kind == "QUESTION_ASPECT_STATUS_BATCH_SET";
    if (question_memory) {
      if (have_meta) close_meta();
      if (!graph_.replay_event(candidate, event, detail))
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_QUESTION_EVENT_INVALID");
      continue;
    }
    if (event.kind == "TEMPORAL_REASSESSMENT_V1") {
      if (have_meta) close_meta();
      TemporalReassessmentRecord record;
      if (!decode_reassessment(event.payload, record) ||
          record.evaluated_state_version + 1 != event.state_version ||
          record.recorded_state_version != event.state_version ||
          !graph_.replay_event(candidate, event, detail))
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_REASSESSMENT_INVALID");
      continue;
    }

    const bool graph_delta_event =
        event.kind == "NODE_ADD" || event.kind == "RELATION_ADD" ||
        event.kind == "SCOPE_SPAN_ADD" || event.kind == "FOCUS_SET" ||
        event.kind == "CLAIM_VALIDITY_SET" ||
        event.kind == "EVIDENCE_VALIDITY_SET" ||
        event.kind == "DEPENDENCY_EVALUATION";
    if (!graph_delta_event) {
      // Events owned by other SURGE domains still advance the shared journal
      // watermark but do not mutate ContextGraph.
      if (have_meta && event.state_version != transaction_version)
        close_meta();
      continue;
    }
    if (!have_meta)
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_DELTA_META_MISSING");
    if (event.kind == "CLAIM_VALIDITY_SET") {
      ClaimValidityRecord record;
      if (!decode_claim_validity(event.payload, record) ||
          record.recorded_state_version != event.state_version)
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_CLAIM_VALIDITY_VERSION_INVALID");
    }
    if (event.kind == "EVIDENCE_VALIDITY_SET") {
      EvidenceValidityRecord record;
      if (!decode_evidence_validity(event.payload, record) ||
          record.recorded_state_version != event.state_version)
        return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_EVIDENCE_VALIDITY_VERSION_INVALID");
    }
    if (!graph_.replay_event(candidate, event, detail))
      return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_DELTA_EVENT_INVALID");
  }
  if (semantic_packet_waiting_for_delta)
    return fail_restore("SNAPSHOT_CONTEXT_GRAPH_TAIL_PACKET_DELTA_MISSING");
  if (error) error->clear();
  return std::make_unique<PreparedContextGraphSnapshotRestore>(
      &graph_, std::move(candidate), std::move(idempotency));
}

} // namespace surge
