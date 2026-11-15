#include "surge/context_graph.hpp"
#include <iomanip>
#include <sstream>
#include <utility>

namespace surge {
namespace {
const char* node_kind_name(NodeKind kind) {
  switch (kind) {
    case NodeKind::Cluster: return "CLUSTER";
    case NodeKind::Entity: return "ENTITY";
    case NodeKind::Evidence: return "EVIDENCE";
    case NodeKind::Claim: return "CLAIM";
    case NodeKind::Question: return "QUESTION";
    case NodeKind::QuestionAspect: return "QUESTION_ASPECT";
    case NodeKind::Dependency: return "DEPENDENCY";
    case NodeKind::Interest: return "INTEREST";
  }
  return "UNKNOWN";
}
bool parse_node_kind(const std::string& value, NodeKind& kind) {
  const std::pair<const char*, NodeKind> values[] = {
    {"CLUSTER",NodeKind::Cluster},{"ENTITY",NodeKind::Entity},{"EVIDENCE",NodeKind::Evidence},
    {"CLAIM",NodeKind::Claim},{"QUESTION",NodeKind::Question},
    {"QUESTION_ASPECT",NodeKind::QuestionAspect},{"DEPENDENCY",NodeKind::Dependency},
    {"INTEREST",NodeKind::Interest}};
  for (const auto& item : values) if (value == item.first) { kind = item.second; return true; }
  return false;
}
const char* relation_kind_name(RelationKind kind) {
  switch (kind) {
    case RelationKind::Supports: return "SUPPORTS";
    case RelationKind::Contradicts: return "CONTRADICTS";
    case RelationKind::AgreesWith: return "AGREES_WITH";
    case RelationKind::ConflictsWith: return "CONFLICTS_WITH";
    case RelationKind::DependsOn: return "DEPENDS_ON";
    case RelationKind::About: return "ABOUT";
  }
  return "UNKNOWN";
}
bool parse_relation_kind(const std::string& value, RelationKind& kind) {
  const std::pair<const char*, RelationKind> values[] = {
    {"SUPPORTS",RelationKind::Supports},{"CONTRADICTS",RelationKind::Contradicts},
    {"AGREES_WITH",RelationKind::AgreesWith},{"CONFLICTS_WITH",RelationKind::ConflictsWith},
    {"DEPENDS_ON",RelationKind::DependsOn},{"ABOUT",RelationKind::About}};
  for (const auto& item : values) if (value == item.first) { kind = item.second; return true; }
  return false;
}
std::string encode_node(const GraphNode& node) {
  std::ostringstream out;
  out << std::quoted(node.id) << ' ' << node_kind_name(node.kind) << ' '
      << std::quoted(node.content) << ' ' << node.references.size();
  for (const auto& ref : node.references) out << ' ' << std::quoted(ref);
  return out.str();
}
bool decode_node(const std::string& payload, GraphNode& node) {
  std::istringstream in(payload); std::string kind; std::size_t count=0;
  if (!(in >> std::quoted(node.id) >> kind >> std::quoted(node.content) >> count) ||
      !parse_node_kind(kind,node.kind) || count>1024) return false;
  for(std::size_t i=0;i<count;++i){std::string ref;if(!(in>>std::quoted(ref)))return false;node.references.push_back(ref);}
  return true;
}
std::string encode_relation(const GraphRelation& relation) {
  std::ostringstream out;
  out << std::quoted(relation.id) << ' ' << relation_kind_name(relation.kind) << ' '
      << std::quoted(relation.from) << ' ' << std::quoted(relation.to);
  return out.str();
}
bool decode_relation(const std::string& payload, GraphRelation& relation) {
  std::istringstream in(payload); std::string kind;
  return static_cast<bool>(in >> std::quoted(relation.id) >> kind >>
      std::quoted(relation.from) >> std::quoted(relation.to)) &&
      parse_relation_kind(kind,relation.kind);
}
std::string encode_focus(const FocusRecord& focus) {
  std::ostringstream out; out << std::quoted(focus.cluster_id) << ' ' << std::quoted(focus.interest_id);
  return out.str();
}
bool decode_focus(const std::string& payload, FocusRecord& focus) {
  std::istringstream in(payload);
  return static_cast<bool>(in >> std::quoted(focus.cluster_id) >> std::quoted(focus.interest_id));
}
std::string hash_text(const std::string& text) {
  std::uint64_t h=1469598103934665603ULL;
  for(unsigned char c:text){h^=c;h*=1099511628211ULL;}
  std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return out.str();
}
}  // namespace

ContextGraph::ContextGraph(std::string path) : journal_(std::move(path)) {}
bool ContextGraph::open() { if(!journal_.open()) return false; return replay(); }

bool ContextGraph::apply_node(State& state,const GraphNode& node,std::string& error) const {
  if(node.id.empty() || node.content.empty() || state.nodes.count(node.id)) {
    error="NODE_ID_OR_CONTENT_INVALID"; return false;
  }
  auto ref_kind=[&](std::size_t index,NodeKind kind){
    return index<node.references.size() && state.nodes.count(node.references[index]) &&
      state.nodes.at(node.references[index]).kind==kind;
  };
  for(const auto& ref:node.references) if(!state.nodes.count(ref)){error="NODE_REFERENCE_NOT_FOUND";return false;}
  switch(node.kind){
    case NodeKind::Cluster: case NodeKind::Entity: case NodeKind::Evidence: case NodeKind::Question:
      if(!node.references.empty()){error="NODE_REFERENCE_SHAPE_INVALID";return false;} break;
    case NodeKind::Claim:
      if(node.references.empty()){error="CLAIM_REQUIRES_EVIDENCE";return false;}
      for(const auto& ref:node.references) if(state.nodes.at(ref).kind!=NodeKind::Evidence){error="CLAIM_EVIDENCE_REF_INVALID";return false;}
      break;
    case NodeKind::QuestionAspect:
      if(node.references.size()!=1 || !ref_kind(0,NodeKind::Question)){error="QUESTION_ASPECT_PARENT_INVALID";return false;}
      break;
    case NodeKind::Dependency:
      if(node.references.size()!=2 || node.references[0]==node.references[1]){error="DEPENDENCY_ENDPOINTS_INVALID";return false;}
      break;
    case NodeKind::Interest:
      if(node.references.size()!=2 || !ref_kind(0,NodeKind::QuestionAspect) || !ref_kind(1,NodeKind::Cluster)){
        error="INTEREST_TARGET_INVALID";return false;
      } break;
  }
  state.nodes.emplace(node.id,node);return true;
}
bool ContextGraph::apply_relation(State& state,const GraphRelation& r,std::string& error) const {
  if(r.id.empty() || state.relations.count(r.id)){error="RELATION_ID_INVALID_OR_DUPLICATE";return false;}
  if(r.from==r.to || !state.nodes.count(r.from) || !state.nodes.count(r.to)){error="RELATION_ENDPOINT_INVALID";return false;}
  const auto from=state.nodes.at(r.from).kind, to=state.nodes.at(r.to).kind;
  switch(r.kind){
    case RelationKind::Supports: case RelationKind::Contradicts:
      if(from!=NodeKind::Claim || to!=NodeKind::Claim){error="CLAIM_RELATION_TYPE_INVALID";return false;} break;
    case RelationKind::AgreesWith: case RelationKind::ConflictsWith:
      if(from!=to || (from!=NodeKind::Cluster && from!=NodeKind::Claim)){error="CLUSTER_RELATION_TYPE_INVALID";return false;} break;
    case RelationKind::About:
      if((from!=NodeKind::Claim && from!=NodeKind::Question && from!=NodeKind::QuestionAspect) ||
         (to!=NodeKind::Entity && to!=NodeKind::Cluster)){error="ABOUT_RELATION_TYPE_INVALID";return false;} break;
    case RelationKind::DependsOn: break;
  }
  state.relations.emplace(r.id,r);return true;
}
bool ContextGraph::apply_focus(State& state,const FocusRecord& f,std::string& error) const {
  if(!state.nodes.count(f.cluster_id) || state.nodes.at(f.cluster_id).kind!=NodeKind::Cluster){
    error="FOCUS_CLUSTER_INVALID";return false;
  }
  if(!f.interest_id.empty()){
    if(!state.nodes.count(f.interest_id) || state.nodes.at(f.interest_id).kind!=NodeKind::Interest ||
       state.nodes.at(f.interest_id).references[1]!=f.cluster_id){error="FOCUS_INTEREST_INVALID";return false;}
  }
  state.focus=f;state.focus_history.push_back(f);return true;
}

std::string ContextGraph::fingerprint(const SemanticDelta& d) const {
  std::ostringstream out;
  out<<std::quoted(d.delta_id)<<' '<<d.expected_state_version<<' ';
  for(const auto& n:d.nodes)out<<"N "<<encode_node(n)<<'\n';
  for(const auto& r:d.relations)out<<"R "<<encode_relation(r)<<'\n';
  if(d.focus)out<<"F "<<encode_focus(d.focus->focus)<<'\n';
  return hash_text(out.str());
}
std::vector<JournalEvent> ContextGraph::encode(const SemanticDelta& d,const std::string& fp) const {
  std::vector<JournalEvent> out;
  std::ostringstream meta;meta<<std::quoted(d.delta_id)<<' '<<std::quoted(d.idempotency_key)<<' '
      <<d.expected_state_version<<' '<<std::quoted(fp);
  out.push_back({d.delta_id+"/meta","DELTA_META",meta.str()});
  std::size_t i=0;
  for(const auto& n:d.nodes)out.push_back({d.delta_id+"/node/"+std::to_string(i++),"NODE_ADD",encode_node(n)});
  i=0;
  for(const auto& r:d.relations)out.push_back({d.delta_id+"/relation/"+std::to_string(i++),"RELATION_ADD",encode_relation(r)});
  if(d.focus)out.push_back({d.delta_id+"/focus","FOCUS_SET",encode_focus(d.focus->focus)});
  return out;
}
bool ContextGraph::replay_event(State& state,const JournalEvent& e,std::string& error) const {
  if(e.kind=="NODE_ADD"){GraphNode n;if(!decode_node(e.payload,n)){error="JOURNAL_NODE_DECODE_FAILED";return false;}return apply_node(state,n,error);}
  if(e.kind=="RELATION_ADD"){GraphRelation r;if(!decode_relation(e.payload,r)){error="JOURNAL_RELATION_DECODE_FAILED";return false;}return apply_relation(state,r,error);}
  if(e.kind=="FOCUS_SET"){FocusRecord f;if(!decode_focus(e.payload,f)){error="JOURNAL_FOCUS_DECODE_FAILED";return false;}return apply_focus(state,f,error);}
  error="JOURNAL_EVENT_KIND_UNKNOWN";return false;
}
bool ContextGraph::replay() {
  State rebuilt;
  idempotency_.clear();
  std::string current_key;
  std::uint64_t replay_version=0;
  bool have_meta=false;
  for(const auto& e:journal_.committed_events()){
    if(e.kind=="DELTA_META"){
      if(have_meta) ++replay_version;
      std::istringstream in(e.payload);
      std::string delta_id, fingerprint;
      std::uint64_t expected=0;
      if(!(in>>std::quoted(delta_id)>>std::quoted(current_key)>>expected>>std::quoted(fingerprint)) ||
         delta_id.empty() || current_key.empty() || fingerprint.empty() || expected!=replay_version)
        return false;
      idempotency_[current_key]=fingerprint;
      have_meta=true;
      continue;
    }
    if(!have_meta || !replay_event(rebuilt,e,last_error_))return false;
  }
  if(have_meta) ++replay_version;
  if(replay_version!=journal_.state_version()) { last_error_="GRAPH_REPLAY_VERSION_MISMATCH"; return false; }
  state_=std::move(rebuilt);return true;
}
DeltaResult ContextGraph::apply(const SemanticDelta& d) {
  if(d.delta_id.empty() || d.idempotency_key.empty() ||
     (d.nodes.empty() && d.relations.empty() && !d.focus))
    return {false,false,state_version(),"DELTA_EMPTY_OR_ID_INVALID"};
  const auto fp=fingerprint(d);
  auto old=idempotency_.find(d.idempotency_key);
  if(old!=idempotency_.end()){
    if(old->second==fp)return {true,true,state_version(),{}};
    return {false,false,state_version(),"IDEMPOTENCY_KEY_REUSED"};
  }
  if(d.expected_state_version!=state_version())return {false,false,state_version(),"STATE_VERSION_CONFLICT"};
  State staged=state_;std::string error;
  for(const auto& n:d.nodes)if(!apply_node(staged,n.node,error))return {false,false,state_version(),error};
  for(const auto& r:d.relations)if(!apply_relation(staged,r.relation,error))return {false,false,state_version(),error};
  if(d.focus && !apply_focus(staged,d.focus->focus,error))return {false,false,state_version(),error};
  auto committed=journal_.commit(d.delta_id,d.idempotency_key,d.expected_state_version,encode(d,fp));
  if(!committed.committed)return {false,false,state_version(),committed.error};
  state_=std::move(staged);idempotency_[d.idempotency_key]=fp;
  return {true,committed.duplicate,state_version(),{}};
}
}  // namespace surge
