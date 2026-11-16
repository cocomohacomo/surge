#include <algorithm>
#include "surge/context_graph.hpp"
#include "surge/scope_validator.hpp"
#include "surge/dependency_engine.hpp"
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

namespace surge {
namespace {
const char* node_kind_name(NodeKind k){switch(k){case NodeKind::Cluster:return "CLUSTER";case NodeKind::Entity:return "ENTITY";case NodeKind::Evidence:return "EVIDENCE";case NodeKind::Claim:return "CLAIM";case NodeKind::Question:return "QUESTION";case NodeKind::QuestionAspect:return "QUESTION_ASPECT";case NodeKind::Dependency:return "DEPENDENCY";case NodeKind::Interest:return "INTEREST";}return "UNKNOWN";}
bool parse_node_kind(const std::string& s,NodeKind& k){const std::pair<const char*,NodeKind> v[]={{"CLUSTER",NodeKind::Cluster},{"ENTITY",NodeKind::Entity},{"EVIDENCE",NodeKind::Evidence},{"CLAIM",NodeKind::Claim},{"QUESTION",NodeKind::Question},{"QUESTION_ASPECT",NodeKind::QuestionAspect},{"DEPENDENCY",NodeKind::Dependency},{"INTEREST",NodeKind::Interest}};for(const auto& x:v)if(s==x.first){k=x.second;return true;}return false;}
const char* relation_kind_name(RelationKind k){switch(k){case RelationKind::Supports:return "SUPPORTS";case RelationKind::Contradicts:return "CONTRADICTS";case RelationKind::AgreesWith:return "AGREES_WITH";case RelationKind::ConflictsWith:return "CONFLICTS_WITH";case RelationKind::DependsOn:return "DEPENDS_ON";case RelationKind::About:return "ABOUT";}return "UNKNOWN";}
bool parse_relation_kind(const std::string& s,RelationKind& k){const std::pair<const char*,RelationKind> v[]={{"SUPPORTS",RelationKind::Supports},{"CONTRADICTS",RelationKind::Contradicts},{"AGREES_WITH",RelationKind::AgreesWith},{"CONFLICTS_WITH",RelationKind::ConflictsWith},{"DEPENDS_ON",RelationKind::DependsOn},{"ABOUT",RelationKind::About}};for(const auto& x:v)if(s==x.first){k=x.second;return true;}return false;}
const char* scope_kind_name(ScopeKind k){switch(k){case ScopeKind::Negation:return "NEGATION";case ScopeKind::Condition:return "CONDITION";case ScopeKind::Modality:return "MODALITY";case ScopeKind::Concession:return "CONCESSION";case ScopeKind::Contrast:return "CONTRAST";case ScopeKind::Coreference:return "COREFERENCE";case ScopeKind::Anaphora:return "ANAPHORA";case ScopeKind::Quotation:return "QUOTATION";case ScopeKind::Temporal:return "TEMPORAL";case ScopeKind::ListContinuation:return "LIST_CONTINUATION";case ScopeKind::TableHeader:return "TABLE_HEADER";}return "UNKNOWN";}
bool parse_scope_kind(const std::string& s,ScopeKind& k){const std::pair<const char*,ScopeKind> v[]={{"NEGATION",ScopeKind::Negation},{"CONDITION",ScopeKind::Condition},{"MODALITY",ScopeKind::Modality},{"CONCESSION",ScopeKind::Concession},{"CONTRAST",ScopeKind::Contrast},{"COREFERENCE",ScopeKind::Coreference},{"ANAPHORA",ScopeKind::Anaphora},{"QUOTATION",ScopeKind::Quotation},{"TEMPORAL",ScopeKind::Temporal},{"LIST_CONTINUATION",ScopeKind::ListContinuation},{"TABLE_HEADER",ScopeKind::TableHeader}};for(const auto& x:v)if(s==x.first){k=x.second;return true;}return false;}
std::string encode_node(const GraphNode& n){std::ostringstream o;o<<std::quoted(n.id)<<' '<<node_kind_name(n.kind)<<' '<<std::quoted(n.content)<<' '<<n.references.size();for(const auto& r:n.references)o<<' '<<std::quoted(r);return o.str();}
bool decode_node(const std::string& p,GraphNode& n){std::istringstream i(p);std::string k;std::size_t c=0;if(!(i>>std::quoted(n.id)>>k>>std::quoted(n.content)>>c)||!parse_node_kind(k,n.kind)||c>1024)return false;for(std::size_t x=0;x<c;++x){std::string r;if(!(i>>std::quoted(r)))return false;n.references.push_back(r);}std::string extra;return !(i>>extra);}
std::string encode_relation(const GraphRelation& r){std::ostringstream o;o<<std::quoted(r.id)<<' '<<relation_kind_name(r.kind)<<' '<<std::quoted(r.from)<<' '<<std::quoted(r.to);return o.str();}
bool decode_relation(const std::string& p,GraphRelation& r){std::istringstream i(p);std::string k,extra;if(!(i>>std::quoted(r.id)>>k>>std::quoted(r.from)>>std::quoted(r.to))||!parse_relation_kind(k,r.kind))return false;return !(i>>extra);}
std::string encode_anchor(const ScopeAnchor& a){std::ostringstream o;o<<std::quoted(a.anchor_id)<<' '<<std::quoted(a.source_digest)<<' '<<std::quoted(a.offset_basis)<<' '<<a.start_offset<<' '<<a.end_offset<<' '<<std::quoted(a.exact_text)<<' '<<std::quoted(a.packet_ref);return o.str();}
bool decode_anchor(std::istream& i,ScopeAnchor& a){return static_cast<bool>(i>>std::quoted(a.anchor_id)>>std::quoted(a.source_digest)>>std::quoted(a.offset_basis)>>a.start_offset>>a.end_offset>>std::quoted(a.exact_text)>>std::quoted(a.packet_ref));}
std::string encode_scope(const AddScopeSpan& add){const auto& s=add.span;std::ostringstream o;o<<std::quoted(s.span_id)<<' '<<scope_kind_name(s.kind)<<' '<<std::quoted(s.source_digest)<<' '<<std::quoted(s.offset_basis)<<' '<<encode_anchor(s.opener)<<' '<<(s.closer?1:0);if(s.closer)o<<' '<<encode_anchor(*s.closer);o<<' '<<s.target_refs.size();for(const auto& x:s.target_refs)o<<' '<<std::quoted(x);o<<' '<<s.antecedent_refs.size();for(const auto& x:s.antecedent_refs)o<<' '<<std::quoted(x);o<<' '<<std::quoted(s.attribution_ref)<<' '<<std::quoted(s.modality_kind)<<' '<<std::quoted(s.temporal_role)<<' '<<std::quoted(s.parent_ref)<<' '<<s.packet_refs.size();for(const auto& x:s.packet_refs)o<<' '<<std::quoted(x);o<<' '<<std::quoted(add.source_snapshot);return o.str();}
bool decode_scope(const std::string& p,AddScopeSpan& add){std::istringstream i(p);auto& s=add.span;std::string kind;int has_closer=0;std::size_t n=0;if(!(i>>std::quoted(s.span_id)>>kind>>std::quoted(s.source_digest)>>std::quoted(s.offset_basis))||!parse_scope_kind(kind,s.kind)||!decode_anchor(i,s.opener)||!(i>>has_closer))return false;if(has_closer!=0&&has_closer!=1)return false;if(has_closer){ScopeAnchor a;if(!decode_anchor(i,a))return false;s.closer=std::move(a);}if(!(i>>n)||n>1024)return false;for(std::size_t x=0;x<n;++x){std::string v;if(!(i>>std::quoted(v)))return false;s.target_refs.push_back(v);}if(!(i>>n)||n>1024)return false;for(std::size_t x=0;x<n;++x){std::string v;if(!(i>>std::quoted(v)))return false;s.antecedent_refs.push_back(v);}if(!(i>>std::quoted(s.attribution_ref)>>std::quoted(s.modality_kind)>>std::quoted(s.temporal_role)>>std::quoted(s.parent_ref)>>n)||n>4096)return false;for(std::size_t x=0;x<n;++x){std::string v;if(!(i>>std::quoted(v)))return false;s.packet_refs.push_back(v);}std::string extra;if(!(i>>std::quoted(add.source_snapshot))||(i>>extra))return false;return true;}
std::string encode_focus(const FocusRecord& f){std::ostringstream o;o<<std::quoted(f.cluster_id)<<' '<<std::quoted(f.interest_id);return o.str();}
bool decode_focus(const std::string& p,FocusRecord& f){std::istringstream i(p);std::string extra;if(!(i>>std::quoted(f.cluster_id)>>std::quoted(f.interest_id)))return false;return !(i>>extra);}
std::string hash_text(const std::string& t){std::uint64_t h=1469598103934665603ULL;for(unsigned char c:t){h^=c;h*=1099511628211ULL;}std::ostringstream o;o<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return o.str();}
std::string encode_dependency_evaluation(const DependencyEvaluationRecord& r) {
 std::ostringstream out;
 out << std::quoted(r.delta_id) << ' ' << r.status << ' '
     << r.iterations << ' ' << r.evaluation_cost << ' '
     << std::quoted(r.diagnostic_error) << ' ' << r.triggered.size();
 for (const auto& item : r.triggered)
  out << ' ' << std::quoted(item.dependency_id) << ' '
      << std::quoted(item.prerequisite_id) << ' ' << std::quoted(item.dependent_id);
 out << ' ' << r.impacted_nodes.size();
 for (const auto& id : r.impacted_nodes) out << ' ' << std::quoted(id);
 out << ' ' << r.rejected_dependencies.size();
 for (const auto& id : r.rejected_dependencies) out << ' ' << std::quoted(id);
 out << ' ' << r.cyclic_components.size();
 for (const auto& component : r.cyclic_components) {
  out << ' ' << component.size();
  for (const auto& id : component) out << ' ' << std::quoted(id);
 }
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
 case NodeKind::Cluster:case NodeKind::Entity:case NodeKind::Evidence:case NodeKind::Question:if(!n.references.empty()){e="NODE_REFERENCE_SHAPE_INVALID";return false;}break;
 case NodeKind::Claim:
  if(n.references.empty()){e="CLAIM_REQUIRES_EVIDENCE";return false;}
  for(const auto& r:n.references){const auto found=s.nodes.find(r);
   if(found==s.nodes.end()||found->second.kind!=NodeKind::Evidence){e="CLAIM_EVIDENCE_REF_INVALID";return false;}}
  break;
 case NodeKind::QuestionAspect:if(n.references.size()!=1||!ref_kind(0,NodeKind::Question)){e="QUESTION_ASPECT_PARENT_INVALID";return false;}break;
 case NodeKind::Dependency:if(n.references.size()!=2||n.references[0]==n.references[1]){e="DEPENDENCY_ENDPOINTS_INVALID";return false;}break;
 case NodeKind::Interest:if(n.references.size()!=2||!ref_kind(0,NodeKind::QuestionAspect)||!ref_kind(1,NodeKind::Cluster)){e="INTEREST_TARGET_INVALID";return false;}break;
 }s.nodes.emplace(n.id,n);return true;
}
bool ContextGraph::apply_relation(State& s,const GraphRelation& r,std::string& e)const{
 if(r.id.empty()||s.relations.count(r.id)){e="RELATION_ID_INVALID_OR_DUPLICATE";return false;}
 if(r.from==r.to||!s.nodes.count(r.from)||!s.nodes.count(r.to)){e="RELATION_ENDPOINT_INVALID";return false;}
 const auto from=s.nodes.find(r.from),to=s.nodes.find(r.to);
 if(from==s.nodes.end()||to==s.nodes.end()){e="RELATION_ENDPOINT_INVALID";return false;}
 auto a=from->second.kind,b=to->second.kind;
 switch(r.kind){case RelationKind::Supports:case RelationKind::Contradicts:if(a!=NodeKind::Claim||b!=NodeKind::Claim){e="CLAIM_RELATION_TYPE_INVALID";return false;}break;
 case RelationKind::AgreesWith:case RelationKind::ConflictsWith:if(a!=b||(a!=NodeKind::Cluster&&a!=NodeKind::Claim)){e="CLUSTER_RELATION_TYPE_INVALID";return false;}break;
 case RelationKind::About:if((a!=NodeKind::Claim&&a!=NodeKind::Question&&a!=NodeKind::QuestionAspect)||(b!=NodeKind::Entity&&b!=NodeKind::Cluster)){e="ABOUT_RELATION_TYPE_INVALID";return false;}break;case RelationKind::DependsOn:break;}
 s.relations.emplace(r.id,r);return true;
}
bool ContextGraph::apply_scope_span(State& s,const AddScopeSpan& add,std::string& e)const{
 if(s.scope_spans.count(add.span.span_id)){e="SCOPE_SPAN_ID_DUPLICATE";return false;}
 const auto result=ScopeSpanValidator::validate(add.span,add.source_snapshot,s.nodes);
 if(!result.accepted){e=result.error.empty()?"SCOPE_SPAN_REJECTED":result.error;return false;}
 s.scope_spans.emplace(add.span.span_id,ScopeSpanRecord{add.span,result.status,result.effect,result.may_emit_unconditional_claim});return true;
}
bool ContextGraph::apply_focus(State& s,const FocusRecord& f,std::string& e)const{
 const auto cluster=s.nodes.find(f.cluster_id);
 if(cluster==s.nodes.end()||cluster->second.kind!=NodeKind::Cluster){e="FOCUS_CLUSTER_INVALID";return false;}
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
 return out;
}
bool ContextGraph::replay_event(State& s,const JournalEvent& e,std::string& error)const{
 if(e.kind=="NODE_ADD"){GraphNode n;if(!decode_node(e.payload,n)){error="JOURNAL_NODE_DECODE_FAILED";return false;}return apply_node(s,n,error);}
 if(e.kind=="RELATION_ADD"){GraphRelation r;if(!decode_relation(e.payload,r)){error="JOURNAL_RELATION_DECODE_FAILED";return false;}return apply_relation(s,r,error);}
 if(e.kind=="SCOPE_SPAN_ADD"){AddScopeSpan span;if(!decode_scope(e.payload,span)){error="JOURNAL_SCOPE_SPAN_DECODE_FAILED";return false;}return apply_scope_span(s,span,error);}
 if(e.kind=="FOCUS_SET"){FocusRecord f;if(!decode_focus(e.payload,f)){error="JOURNAL_FOCUS_DECODE_FAILED";return false;}return apply_focus(s,f,error);}
 if(e.kind=="DEPENDENCY_EVALUATION"||e.kind=="DEPENDENCY_EVALUATION_DIAGNOSTIC"){
  std::istringstream in(e.payload); DependencyEvaluationRecord record;
  std::size_t count=0;
  if(!(in>>std::quoted(record.delta_id)>>record.status>>record.iterations>>
       record.evaluation_cost>>std::quoted(record.diagnostic_error)>>count)||
     record.delta_id.empty()||
     (record.status!="CONVERGED"&&record.status!="DEFERRED"&&record.status!="INVALID")||
     count>4096){
   error="JOURNAL_DEPENDENCY_EVALUATION_INVALID";return false;
  }
  const auto prior=s.dependency_evaluations.find(record.delta_id);
  if(prior!=s.dependency_evaluations.end()&&
     !(e.kind=="DEPENDENCY_EVALUATION"&&prior->second.status!="CONVERGED"&&
       record.status=="CONVERGED")){
   error="JOURNAL_DEPENDENCY_EVALUATION_DUPLICATE";return false;
  }
  if((e.kind=="DEPENDENCY_EVALUATION"&&record.status!="CONVERGED")||
     (e.kind=="DEPENDENCY_EVALUATION_DIAGNOSTIC"&&record.status=="CONVERGED")){
   error="JOURNAL_DEPENDENCY_EVALUATION_STATUS_INVALID";return false;
  }
  for(std::size_t i=0;i<count;++i){
   DependencyTrigger trigger;
   if(!(in>>std::quoted(trigger.dependency_id)>>std::quoted(trigger.prerequisite_id)>>
        std::quoted(trigger.dependent_id))||trigger.dependency_id.empty()||
      trigger.prerequisite_id.empty()||trigger.dependent_id.empty()){
    error="JOURNAL_DEPENDENCY_TRIGGER_INVALID";return false;
   }
   if(e.kind=="DEPENDENCY_EVALUATION"){
    const auto dependency=s.nodes.find(trigger.dependency_id);
    if(dependency==s.nodes.end()||dependency->second.kind!=NodeKind::Dependency||
       dependency->second.references.size()!=2||
       dependency->second.references[0]!=trigger.prerequisite_id||
       dependency->second.references[1]!=trigger.dependent_id){
     error="JOURNAL_DEPENDENCY_TRIGGER_INVALID";return false;
    }
   }
   record.triggered.push_back(std::move(trigger));
  }
  if(!(in>>count)||count>4096){error="JOURNAL_DEPENDENCY_IMPACT_INVALID";return false;}
  for(std::size_t i=0;i<count;++i){std::string id;if(!(in>>std::quoted(id))||id.empty()||
      (e.kind=="DEPENDENCY_EVALUATION"&&!s.nodes.count(id))){error="JOURNAL_DEPENDENCY_IMPACT_INVALID";return false;}record.impacted_nodes.push_back(std::move(id));}
  if(!(in>>count)||count>4096){error="JOURNAL_DEPENDENCY_REJECTION_INVALID";return false;}
  for(std::size_t i=0;i<count;++i){
   std::string id;if(!(in>>std::quoted(id))){error="JOURNAL_DEPENDENCY_REJECTION_INVALID";return false;}
   const auto dependency=s.nodes.find(id);
   if(id.empty()||(e.kind=="DEPENDENCY_EVALUATION"&&
      (dependency==s.nodes.end()||dependency->second.kind!=NodeKind::Dependency))){
    error="JOURNAL_DEPENDENCY_REJECTION_INVALID";return false;
   }
   record.rejected_dependencies.push_back(std::move(id));
  }
  if(!(in>>count)||count>4096){error="JOURNAL_DEPENDENCY_SCC_INVALID";return false;}
  for(std::size_t i=0;i<count;++i){
   std::size_t members=0;
   if(!(in>>members)||members>4096){error="JOURNAL_DEPENDENCY_SCC_INVALID";return false;}
   std::vector<std::string> component;
   for(std::size_t j=0;j<members;++j){
    std::string id;
    if(!(in>>std::quoted(id))||id.empty()||
       (e.kind=="DEPENDENCY_EVALUATION"&&!s.nodes.count(id))){error="JOURNAL_DEPENDENCY_SCC_INVALID";return false;}
    component.push_back(std::move(id));
   }
   if(!std::is_sorted(component.begin(),component.end())||
      std::adjacent_find(component.begin(),component.end())!=component.end()){
    error="JOURNAL_DEPENDENCY_SCC_ORDER_INVALID";return false;
   }
   record.cyclic_components.push_back(std::move(component));
  }
  std::string extra;if(in>>extra){error="JOURNAL_DEPENDENCY_EVALUATION_TRAILING_DATA";return false;}
  s.dependency_evaluations[record.delta_id]=std::move(record);return true;
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
   if(have_meta||semantic_packet_waiting_for_delta){last_error_="JOURNAL_SEMANTIC_PACKET_ORDER_INVALID";return false;}
   semantic_packet_waiting_for_delta=true;continue;
  }
  if(e.kind=="RETRIEVAL_TICKET_ISSUED"||e.kind=="RETRIEVAL_TICKET_RESOLVED"){if(have_meta){++version;have_meta=false;}++version;continue;}
  if(e.kind=="DELTA_META"){if(have_meta){++version;have_meta=false;}std::istringstream i(e.payload);std::string id,fp;std::uint64_t expected=0;
   if(!(i>>std::quoted(id)>>std::quoted(key)>>expected>>std::quoted(fp))||id.empty()||key.empty()||fp.empty()||expected!=version){last_error_="JOURNAL_DELTA_META_INVALID";return false;}idempotency_[key]=fp;have_meta=true;semantic_packet_waiting_for_delta=false;continue;}
  if(e.kind=="DEPENDENCY_EVALUATION_DIAGNOSTIC"){
   if(have_meta){++version;have_meta=false;}
   if(!replay_event(rebuilt,e,last_error_))return false;
   ++version;continue;
  }
  if(e.kind=="QUESTION_ASPECT_STATUS_SET"){if(have_meta){++version;have_meta=false;}if(!replay_event(rebuilt,e,last_error_)){last_error_="JOURNAL_QUESTION_ASPECT_STATUS_INVALID";return false;}++version;continue;}
  if(e.kind=="NODE_ADD"||e.kind=="RELATION_ADD"||e.kind=="SCOPE_SPAN_ADD"||e.kind=="FOCUS_SET"||e.kind=="DEPENDENCY_EVALUATION"){if(!have_meta||!replay_event(rebuilt,e,last_error_))return false;}
 }
 if(semantic_packet_waiting_for_delta){last_error_="JOURNAL_SEMANTIC_PACKET_DELTA_MISSING";return false;}
 if(have_meta) { ++version; }
 if(version!=journal_->state_version()){last_error_="GRAPH_REPLAY_VERSION_MISMATCH";return false;}
 state_=std::move(rebuilt);
 return true;
}

bool ContextGraph::prepare_transaction(const SemanticDelta& d, State& staged,
    std::vector<JournalEvent>& events, std::string& error,
    DependencyEvaluationRecord* diagnostic) const {
 events.clear(); error.clear();
 if(d.delta_id.empty()||d.idempotency_key.empty()||
    (d.nodes.empty()&&d.relations.empty()&&d.scope_spans.empty()&&!d.focus)){
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
  const auto evaluation=DependencyEngine::evaluate_fixpoint(staged.nodes,
      std::vector<std::string>(changed.begin(),changed.end()),4096,4096,1000000);
  DependencyEvaluationRecord record;
  record.delta_id=d.delta_id;
  record.status=evaluation.status==FixpointStatus::Converged?"CONVERGED":
      evaluation.status==FixpointStatus::Deferred?"DEFERRED":"INVALID";
  record.iterations=evaluation.iterations;
  record.evaluation_cost=evaluation.evaluation_cost;
  record.impacted_nodes=evaluation.impacted_nodes;
  record.rejected_dependencies=evaluation.rejected_dependencies;
  record.cyclic_components=evaluation.cyclic_components;
  record.diagnostic_error=evaluation.error;
  if(evaluation.status!=FixpointStatus::Converged){
   if(diagnostic)*diagnostic=record;
   error=evaluation.error.empty()?"DEPENDENCY_FIXPOINT_DEFERRED":evaluation.error;
   events.clear();return false;
  }
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

DeltaResult ContextGraph::apply(const SemanticDelta& d){
 State staged;std::vector<JournalEvent> events;std::string error;
 DependencyEvaluationRecord diagnostic;
 if(!prepare_transaction(d,staged,events,error,&diagnostic)){
  if(!diagnostic.delta_id.empty()){
   const auto existing=state_.dependency_evaluations.find(diagnostic.delta_id);
   if(existing==state_.dependency_evaluations.end()){
    const std::string txid="dependency-diagnostic/"+diagnostic.delta_id;
    const std::string key="dependency-diagnostic/"+diagnostic.delta_id;
    const auto committed=journal_->commit(txid,key,state_version(),
        {{txid+"/record","DEPENDENCY_EVALUATION_DIAGNOSTIC",
          encode_dependency_evaluation(diagnostic)}});
    if(committed.committed){
     state_.dependency_evaluations[diagnostic.delta_id]=diagnostic;
    } else if(error.empty()) error=committed.error;
   }
  }
  return {false,false,state_version(),error};
 }
 if(events.empty())return {true,true,state_version(),{}};
 const auto committed=journal_->commit(d.delta_id,d.idempotency_key,d.expected_state_version,events);
 if(!committed.committed)return {false,false,state_version(),committed.error};
 state_=std::move(staged);idempotency_[d.idempotency_key]=fingerprint(d);
 return {true,committed.duplicate,state_version(),{}};
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
  if (question_aspect_status(id) == status) return true;
  if (expected != state_version()) { error = "STATE_VERSION_CONFLICT"; return false; }
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

} // namespace surge
