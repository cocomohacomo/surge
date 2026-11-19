#include "surge/cluster_retention.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
namespace surge {
namespace {
bool is_cluster(NodeKind kind) { return kind == NodeKind::Cluster || kind == NodeKind::ChaosCluster; }
void add_ancestors(const std::string& cluster,const std::map<std::string,std::vector<std::string>>& parents,std::set<std::string>& protected_ids) {
 std::vector<std::string> pending{cluster};
 while(!pending.empty()){auto current=std::move(pending.back());pending.pop_back();if(!protected_ids.insert(current).second)continue;const auto found=parents.find(current);if(found!=parents.end())for(const auto& parent:found->second)pending.push_back(parent);}
}
}
ClusterRetentionPlan ClusterRetentionPlanner::plan(const ContextGraph& graph,const std::vector<ClusterRetentionCandidate>& candidates,std::size_t max_retained_clusters) {
 ClusterRetentionPlan result;result.planned_state_version=graph.state_version();std::map<std::string,ClusterRetentionCandidate> by_id;
 for(const auto& candidate:candidates){
  if(candidate.cluster_id.empty()||!std::isfinite(candidate.retention_priority)){result.error="CLUSTER_RETENTION_CANDIDATE_INVALID";return result;}
  const auto node=graph.nodes().find(candidate.cluster_id);
  if(node==graph.nodes().end()||!is_cluster(node->second.kind)){result.error="CLUSTER_RETENTION_TARGET_NOT_FOUND";return result;}
  if(!by_id.emplace(candidate.cluster_id,candidate).second){result.error="CLUSTER_RETENTION_DUPLICATE_CANDIDATE";return result;}
 }
 std::set<std::string> dormant_ids, inactive_ids;
 for(const auto& candidate:candidates){
  const auto lifecycle=graph.cluster_lifecycle(candidate.cluster_id);
  if(!lifecycle)continue;
  if(lifecycle->state==ClusterLifecycleState::Dormant)dormant_ids.insert(candidate.cluster_id);
  else if(lifecycle->state==ClusterLifecycleState::Archived||lifecycle->state==ClusterLifecycleState::Tombstoned)
   inactive_ids.insert(candidate.cluster_id);
 }
 std::size_t graph_cluster_count=0;
 for(const auto& item:graph.nodes())if(is_cluster(item.second.kind)){++graph_cluster_count;if(!by_id.count(item.first)){result.error="CLUSTER_RETENTION_CANDIDATE_SET_INCOMPLETE";return result;}}
 if(by_id.size()!=graph_cluster_count){result.error="CLUSTER_RETENTION_CANDIDATE_SET_INCOMPLETE";return result;}
 std::map<std::string,std::vector<std::string>> parents,memberships;
 for(const auto& item:graph.relations()){const auto& relation=item.second;if(relation.kind!=RelationKind::Contains)continue;const auto from=graph.nodes().find(relation.from);const auto to=graph.nodes().find(relation.to);if(from==graph.nodes().end()||to==graph.nodes().end()||!is_cluster(from->second.kind))continue;memberships[relation.to].push_back(relation.from);if(is_cluster(to->second.kind))parents[relation.to].push_back(relation.from);}
 std::set<std::string> protected_ids;if(graph.focus())add_ancestors(graph.focus()->cluster_id,parents,protected_ids);
 std::set<std::string> unresolved_questions;
 for(const auto& item:graph.nodes()){const auto& node=item.second;if(node.kind==NodeKind::Question&&graph.question_memory_state(node.id)==QuestionMemoryState::Active)unresolved_questions.insert(node.id);}
 for(const auto& item:graph.nodes()){const auto& node=item.second;if(node.kind!=NodeKind::QuestionAspect||node.references.size()!=1)continue;const auto status=graph.question_aspect_status(node.id);if(status==QuestionAspectStatus::Open||status==QuestionAspectStatus::Deferred)unresolved_questions.insert(node.references.front());}
 for(const auto& question_id:unresolved_questions){const auto found=memberships.find(question_id);if(found!=memberships.end())for(const auto& cluster_id:found->second)add_ancestors(cluster_id,parents,protected_ids);}
 const auto anchors=protected_ids;std::set<std::string> dependency_neighbors;
 for(const auto& item:graph.relations()){const auto& relation=item.second;if(relation.kind!=RelationKind::DependsOn)continue;const auto from=graph.nodes().find(relation.from);const auto to=graph.nodes().find(relation.to);if(from==graph.nodes().end()||to==graph.nodes().end()||!is_cluster(from->second.kind)||!is_cluster(to->second.kind))continue;if(anchors.count(relation.from))dependency_neighbors.insert(relation.to);if(anchors.count(relation.to))dependency_neighbors.insert(relation.from);}
 for(const auto& id:dependency_neighbors)add_ancestors(id,parents,protected_ids);
 for(const auto& item:by_id)if(item.second.pinned)add_ancestors(item.first,parents,protected_ids);
 for(const auto& id:protected_ids)if(inactive_ids.count(id)){
  result.error="CLUSTER_RETENTION_PROTECTED_INACTIVE_CLUSTER";return result;
 }
 std::vector<ClusterRetentionCandidate> ranked;ranked.reserve(by_id.size());for(const auto& item:by_id){
  if(inactive_ids.count(item.first))result.already_inactive_cluster_ids.push_back(item.first);
  else if(!protected_ids.count(item.first))ranked.push_back(item.second);
 }
 std::sort(ranked.begin(),ranked.end(),[&](const auto& left,const auto& right){
  const bool left_dormant=dormant_ids.count(left.cluster_id)!=0;
  const bool right_dormant=dormant_ids.count(right.cluster_id)!=0;
  if(left_dormant!=right_dormant)return !left_dormant;
  if(left.retention_priority!=right.retention_priority)return left.retention_priority>right.retention_priority;if(left.last_access_sequence!=right.last_access_sequence)return left.last_access_sequence>right.last_access_sequence;return left.cluster_id<right.cluster_id;});
 std::size_t protected_count=0;for(const auto& id:protected_ids)if(by_id.count(id)){result.protected_cluster_ids.push_back(id);++protected_count;}
 if(protected_count>max_retained_clusters){result.error="CLUSTER_RETENTION_PROTECTED_FLOOR_EXCEEDED";result.protected_cluster_ids.clear();return result;}
 const auto available=max_retained_clusters-protected_count;std::set<std::string> selected;
 for(std::size_t i=0;i<ranked.size();++i){if(i<available)selected.insert(ranked[i].cluster_id);else result.forget_cluster_ids.push_back(ranked[i].cluster_id);}
 result.retained_cluster_ids=result.protected_cluster_ids;for(const auto& candidate:ranked)if(selected.count(candidate.cluster_id))result.retained_cluster_ids.push_back(candidate.cluster_id);
 // A member with any retained membership must not be reclaimed when another
 // membership's cluster is forgotten.
 const std::set<std::string> retained(result.retained_cluster_ids.begin(),result.retained_cluster_ids.end()),forgotten(result.forget_cluster_ids.begin(),result.forget_cluster_ids.end());
 for(const auto& item:memberships){bool in_retained=false,in_forgotten=false;for(const auto& cluster_id:item.second){in_retained=in_retained||retained.count(cluster_id)!=0;in_forgotten=in_forgotten||forgotten.count(cluster_id)!=0;}if(in_retained&&in_forgotten)result.preserved_shared_member_ids.push_back(item.first);}
 std::sort(result.protected_cluster_ids.begin(),result.protected_cluster_ids.end());std::sort(result.retained_cluster_ids.begin(),result.retained_cluster_ids.end());std::sort(result.forget_cluster_ids.begin(),result.forget_cluster_ids.end());std::sort(result.preserved_shared_member_ids.begin(),result.preserved_shared_member_ids.end());
 std::sort(result.already_inactive_cluster_ids.begin(),result.already_inactive_cluster_ids.end());
 result.accepted=true;return result;
}
}
