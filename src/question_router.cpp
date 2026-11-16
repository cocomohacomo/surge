#include "surge/question_router.hpp"
#include <algorithm>
#include <set>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace surge {
QuestionRouteResult QuestionRouter::route(ContextGraph& graph,const std::string& relation_id,
                                            const std::string& policy_version) {
  QuestionRouteResult result;
  result.state_version=graph.state_version();
  if(relation_id.empty() || policy_version.empty()){
    result.error="QUESTION_ROUTE_REQUEST_INVALID";return result;
  }
  const auto rel_it=graph.relations().find(relation_id);
  if(rel_it==graph.relations().end() || rel_it->second.kind!=RelationKind::Contradicts){
    result.error="CONTRADICTION_RELATION_NOT_FOUND";return result;
  }
  const auto& contradiction=rel_it->second;
  const auto from=graph.nodes().find(contradiction.from);
  const auto to=graph.nodes().find(contradiction.to);
  if(from==graph.nodes().end() || to==graph.nodes().end() ||
     from->second.kind!=NodeKind::Claim || to->second.kind!=NodeKind::Claim){
    result.error="CONTRADICTION_CLAIMS_INVALID";return result;
  }
  result.question_id="question/contradiction/"+relation_id;
  result.question_aspect_id="aspect/contradiction/"+relation_id;
  const auto existing=graph.nodes().find(result.question_id);
  if(existing!=graph.nodes().end()){
    if(existing->second.kind!=NodeKind::Question ||
       !graph.nodes().count(result.question_aspect_id)){
      result.error="QUESTION_ROUTE_STATE_INCOMPLETE";return result;
    }
    for(const auto& item:graph.nodes()){
      if(item.second.kind==NodeKind::Interest && item.second.references.size()==2 &&
         item.second.references[0]==result.question_aspect_id)
        result.interest_ids.push_back(item.first);
    }
    std::sort(result.interest_ids.begin(),result.interest_ids.end());
    result.committed=true;result.duplicate=true;result.state_version=graph.state_version();
    result.focus_selected=graph.focus().has_value() &&
      std::find(result.interest_ids.begin(),result.interest_ids.end(),
                graph.focus()->interest_id)!=result.interest_ids.end();
    return result;
  }

  std::set<std::string> clusters;
  for(const auto& relation:graph.relations()){
    if(relation.second.kind==RelationKind::About &&
       (relation.second.from==contradiction.from || relation.second.from==contradiction.to) &&
       graph.nodes().count(relation.second.to) &&
       graph.nodes().at(relation.second.to).kind==NodeKind::Cluster)
      clusters.insert(relation.second.to);
  }
  SemanticDelta delta;
  delta.delta_id="question-route/"+relation_id;
  delta.idempotency_key="question-route/"+relation_id;
  delta.expected_state_version=graph.state_version();
  delta.nodes.push_back({{result.question_id,NodeKind::Question,
      "矛盾の整理が必要: "+contradiction.from+" / "+contradiction.to,{} }});
  delta.nodes.push_back({{result.question_aspect_id,NodeKind::QuestionAspect,
      "矛盾する主張の妥当性・条件・時点を確認する。policy="+policy_version,
      {result.question_id}}});
  for(const auto& cluster:clusters){
    const auto interest="interest/contradiction/"+relation_id+"/"+cluster;
    result.interest_ids.push_back(interest);
    delta.nodes.push_back({{interest,NodeKind::Interest,
      "矛盾関係の確認対象クラスター",{result.question_aspect_id,cluster}}});
  }
  if(clusters.size()==1){
    delta.focus=SetFocus{{*clusters.begin(),result.interest_ids.front()}};
    result.focus_selected=true;
  }
  const auto applied=graph.apply(delta);
  result.committed=applied.committed;result.duplicate=applied.duplicate;
  result.state_version=applied.state_version;result.error=applied.error;
  if(!applied.committed)result.focus_selected=false;
  return result;
}
QuestionRouteResult QuestionRouter::route_scoped(ContextGraph& graph,
    const std::string& relation_id, const std::string& policy_version,
    const std::string& scope_key) {
  QuestionRouteResult result; result.state_version = graph.state_version();
  if (relation_id.empty() || policy_version.empty() || scope_key.empty()) { result.error = "SCOPED_QUESTION_ROUTE_REQUEST_INVALID"; return result; }
  const auto ri = graph.relations().find(relation_id);
  if (ri == graph.relations().end() || ri->second.kind != RelationKind::Contradicts) { result.error = "CONTRADICTION_RELATION_NOT_FOUND"; return result; }
  const auto& rel = ri->second;
  const auto a = graph.nodes().find(rel.from), b = graph.nodes().find(rel.to);
  if (a == graph.nodes().end() || b == graph.nodes().end() || a->second.kind != NodeKind::Claim || b->second.kind != NodeKind::Claim) { result.error = "CONTRADICTION_CLAIMS_INVALID"; return result; }
  const std::string left = std::min(rel.from, rel.to), right = std::max(rel.from, rel.to);
  const std::string identity = left + "\n" + right + "\n" + scope_key;
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char ch : identity) { hash ^= ch; hash *= 1099511628211ULL; }
  std::ostringstream suffix; suffix << std::hex << std::setw(16) << std::setfill('0') << hash;
  result.question_id = "question/contradiction-scope/" + suffix.str();
  result.question_aspect_id = result.question_id + "/aspect/" + relation_id;
  const auto parent = graph.nodes().find(result.question_id);
  if (parent != graph.nodes().end() && parent->second.kind != NodeKind::Question) { result.error = "QUESTION_ROUTE_STATE_INCOMPLETE"; return result; }
  const auto aspect = graph.nodes().find(result.question_aspect_id);
  if (aspect != graph.nodes().end()) {
    if (aspect->second.kind != NodeKind::QuestionAspect || aspect->second.references.size() != 1 || aspect->second.references.front() != result.question_id) { result.error = "QUESTION_ROUTE_STATE_INCOMPLETE"; return result; }
    for (const auto& item : graph.nodes()) if (item.second.kind == NodeKind::Interest && item.second.references.size() == 2 && item.second.references.front() == result.question_aspect_id) result.interest_ids.push_back(item.first);
    std::sort(result.interest_ids.begin(), result.interest_ids.end()); result.committed = true; result.duplicate = true; result.state_version = graph.state_version();
    result.focus_selected = graph.focus() && std::find(result.interest_ids.begin(), result.interest_ids.end(), graph.focus()->interest_id) != result.interest_ids.end(); return result;
  }
  std::set<std::string> clusters;
  for (const auto& item : graph.relations()) if (item.second.kind == RelationKind::About && (item.second.from == rel.from || item.second.from == rel.to) && graph.nodes().count(item.second.to) && graph.nodes().at(item.second.to).kind == NodeKind::Cluster) clusters.insert(item.second.to);
  SemanticDelta delta; delta.delta_id = "question-route-scoped/" + suffix.str() + "/" + relation_id; delta.idempotency_key = delta.delta_id; delta.expected_state_version = graph.state_version();
  if (parent == graph.nodes().end()) delta.nodes.push_back({{result.question_id, NodeKind::Question, "同一Claim対・同一スコープに属する矛盾: " + left + " / " + right, {}}});
  delta.nodes.push_back({{result.question_aspect_id, NodeKind::QuestionAspect, "矛盾イベント=" + relation_id + "; scope=" + scope_key + "; policy=" + policy_version, {result.question_id}}});
  for (const auto& cluster : clusters) { const auto interest = "interest/" + result.question_aspect_id + "/" + cluster; result.interest_ids.push_back(interest); delta.nodes.push_back({{interest, NodeKind::Interest, "矛盾イベント=" + relation_id + " の確認対象", {result.question_aspect_id, cluster}}}); }
  if (clusters.size() == 1) { delta.focus = SetFocus{{*clusters.begin(), result.interest_ids.front()}}; result.focus_selected = true; }
  const auto applied = graph.apply(delta); result.committed = applied.committed; result.duplicate = applied.duplicate; result.state_version = applied.state_version; result.error = applied.error; if (!applied.committed) result.focus_selected = false; return result;
}
}  // namespace surge
