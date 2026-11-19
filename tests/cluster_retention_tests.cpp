#include "surge/cluster_retention.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
using namespace surge;
int main(){
 const std::string path="cluster_retention_test.journal";std::remove(path.c_str());
 {
  EventJournal journal(path);assert(journal.open());ContextGraph graph(journal);assert(graph.open());
  SemanticDelta seed;seed.delta_id="cluster-retention-seed";seed.idempotency_key="cluster-retention-seed-key";seed.expected_state_version=graph.state_version();
  seed.nodes={
   {{"root",NodeKind::Cluster,"stream root",{}}},{{"focus-child",NodeKind::Cluster,"active focus",{}}},
   {{"question-child",NodeKind::Cluster,"unresolved question",{}}},{{"dependency-child",NodeKind::Cluster,"one-hop dependency",{}}},
   {{"low-a",NodeKind::Cluster,"low importance",{}}},{{"low-b",NodeKind::ChaosCluster,"low importance chaos",{}}},
   {{"q-open",NodeKind::Question,"unresolved question",{}}},{{"aspect-open",NodeKind::QuestionAspect,"open aspect",{"q-open"}}},
   {{"focus-interest",NodeKind::Interest,"active focus interest",{"aspect-open","focus-child"}}},
   {{"shared-evidence",NodeKind::Evidence,"shared provenance",{}}}
  };
  seed.relations={
   {{"root-focus",RelationKind::Contains,"root","focus-child"}},{{"root-question",RelationKind::Contains,"root","question-child"}},
   {{"root-dependency",RelationKind::Contains,"root","dependency-child"}},{{"root-low-a",RelationKind::Contains,"root","low-a"}},
   {{"root-low-b",RelationKind::Contains,"root","low-b"}},{{"question-membership",RelationKind::Contains,"question-child","q-open"}},
   {{"focus-dependency",RelationKind::DependsOn,"focus-child","dependency-child"}},
   {{"focus-interest-membership",RelationKind::Contains,"focus-child","focus-interest"}},
   {{"shared-a",RelationKind::Contains,"low-a","shared-evidence"}},{{"shared-b",RelationKind::Contains,"low-b","shared-evidence"}}
  };
  seed.focus=SetFocus{{"focus-child","focus-interest",false}};assert(graph.apply(seed).committed);
  const std::vector<ClusterRetentionCandidate> candidates={
   {"root",0.0,1,false},{"focus-child",0.0,1,false},{"question-child",0.0,1,false},
   {"dependency-child",0.0,1,false},{"low-a",0.1,1,false},{"low-b",0.9,1,false}
  };
  const auto plan=ClusterRetentionPlanner::plan(graph,candidates,5);assert(plan.accepted);
  assert(plan.retained_cluster_ids.size()==5);assert(plan.forget_cluster_ids.size()==1);
  assert(plan.forget_cluster_ids.front()=="low-a");assert(plan.protected_cluster_ids.size()==4);
  assert(plan.preserved_shared_member_ids.size()==1);assert(plan.preserved_shared_member_ids.front()=="shared-evidence");
  assert(graph.nodes().count("low-a")==1);
  const auto floor=ClusterRetentionPlanner::plan(graph,candidates,3);assert(!floor.accepted);
  assert(floor.error=="CLUSTER_RETENTION_PROTECTED_FLOOR_EXCEEDED");
  auto invalid=candidates;invalid.push_back({"low-a",1.0,2,false});
  const auto duplicate=ClusterRetentionPlanner::plan(graph,invalid,10);assert(!duplicate.accepted);
  assert(duplicate.error=="CLUSTER_RETENTION_DUPLICATE_CANDIDATE");
  invalid=candidates;invalid.front().retention_priority=std::numeric_limits<double>::quiet_NaN();
  const auto non_finite=ClusterRetentionPlanner::plan(graph,invalid,10);assert(!non_finite.accepted);
  assert(non_finite.error=="CLUSTER_RETENTION_CANDIDATE_INVALID");
  auto incomplete=candidates;incomplete.pop_back();
  const auto missing=ClusterRetentionPlanner::plan(graph,incomplete,10);assert(!missing.accepted);
  assert(missing.error=="CLUSTER_RETENTION_CANDIDATE_SET_INCOMPLETE");
 }
 std::remove(path.c_str());return 0;
}
