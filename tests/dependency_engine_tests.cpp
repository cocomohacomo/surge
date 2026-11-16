#include "surge/dependency_engine.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>
using namespace surge;
int main(){
  std::map<std::string,GraphNode> nodes;
  auto add=[&](const std::string& id,NodeKind kind,std::vector<std::string> refs={}){
    nodes.emplace(id,GraphNode{id,kind,id,std::move(refs)});
  };
  add("A",NodeKind::Cluster);add("B",NodeKind::Cluster);add("C",NodeKind::Claim);
  add("D",NodeKind::Question);add("E",NodeKind::Cluster);
  add("dep-1",NodeKind::Dependency,{"A","B"});
  add("dep-2",NodeKind::Dependency,{"B","C"});
  add("dep-3",NodeKind::Dependency,{"C","D"});
  add("dep-4",NodeKind::Dependency,{"D","B"});
  add("dep-5",NodeKind::Dependency,{"C","E"});
  auto chain=DependencyEngine::compute(nodes,{"A"},10);
  assert(chain.complete && chain.impacted_nodes.size()==5);
  assert(chain.cyclic_components.size()==1);
  assert((chain.cyclic_components[0]==std::vector<std::string>{"B","C","D"}));
  auto repeat=DependencyEngine::compute(nodes,{"A"},10);
  assert(repeat.impacted_nodes==chain.impacted_nodes && repeat.cyclic_components==chain.cyclic_components);
  auto limited=DependencyEngine::compute(nodes,{"A"},3);
  assert(!limited.complete && limited.error=="IMPACT_BUDGET_EXCEEDED");
  auto missing=DependencyEngine::compute(nodes,{"MISSING"},10);
  assert(!missing.complete && missing.error=="IMPACT_ROOT_NOT_FOUND");
  auto empty=DependencyEngine::compute(nodes,{},10);
  assert(!empty.complete && empty.error=="IMPACT_REQUEST_INVALID");
  auto invalid_nodes=nodes;
  invalid_nodes["dep-1"].references={"A","NOPE"};
  auto invalid=DependencyEngine::compute(invalid_nodes,{"A"},10);
  assert(!invalid.complete && invalid.error=="DEPENDENCY_RECORD_INVALID");
  auto eval_nodes = nodes;
  eval_nodes["A"].content = "approved";
  eval_nodes["dep-1"].content = "CONTENT_EQUALS:approved";
  eval_nodes["dep-2"].content = "ON_CHANGE";
  eval_nodes["dep-3"].content = "CONTENT_CONTAINS:urgent";
  eval_nodes["dep-4"].content = "UNKNOWN:unsafe";
  eval_nodes["dep-5"].content = "KIND:CLAIM";
  eval_nodes["C"].content = "urgent claim";
  auto evaluated = DependencyEngine::evaluate(eval_nodes, {"A"}, 10);
  assert(evaluated.complete);
  assert((evaluated.triggered_dependencies == std::vector<std::string>{"dep-1", "dep-2", "dep-3", "dep-5"}));
  assert((evaluated.rejected_dependencies == std::vector<std::string>{"dep-4"}));
  assert(std::find(evaluated.impacted_nodes.begin(), evaluated.impacted_nodes.end(), "E") != evaluated.impacted_nodes.end());
  auto no_change = DependencyEngine::evaluate(eval_nodes, {"B"}, 10);
  assert(no_change.complete);
  assert(std::find(no_change.triggered_dependencies.begin(), no_change.triggered_dependencies.end(), "dep-1") == no_change.triggered_dependencies.end());
  auto eval_limited = DependencyEngine::evaluate(eval_nodes, {"A"}, 2);
  assert(!eval_limited.complete && eval_limited.error == "DEPENDENCY_EVALUATION_BUDGET_EXCEEDED");
  eval_nodes["dep-empty-equals"] = GraphNode{"dep-empty-equals", NodeKind::Dependency,
      "CONTENT_EQUALS:", {"A", "E"}};
  eval_nodes["dep-empty-contains"] = GraphNode{"dep-empty-contains", NodeKind::Dependency,
      "CONTENT_CONTAINS:", {"A", "E"}};
  eval_nodes["dep-empty-kind"] = GraphNode{"dep-empty-kind", NodeKind::Dependency,
      "KIND:", {"A", "E"}};
  eval_nodes["dep-unknown-kind"] = GraphNode{"dep-unknown-kind", NodeKind::Dependency,
      "KIND:UNKNOWN", {"A", "E"}};
  auto malformed = DependencyEngine::evaluate(eval_nodes, {"A"}, 10);
  assert(malformed.complete);
  assert((malformed.rejected_dependencies == std::vector<std::string>{
      "dep-4", "dep-empty-contains", "dep-empty-equals", "dep-empty-kind",
      "dep-unknown-kind"}));
  assert(std::find(malformed.triggered_dependencies.begin(),
      malformed.triggered_dependencies.end(), "dep-empty-equals") ==
      malformed.triggered_dependencies.end());
  std::map<std::string,GraphNode> budget_nodes;
  budget_nodes.emplace("root", GraphNode{"root",NodeKind::Cluster,"root",{}});
  for (int i=0;i<100;++i) {
    const auto target="target-"+std::to_string(i);
    const auto dependency="budget-dep-"+std::to_string(i);
    budget_nodes.emplace(target,GraphNode{target,NodeKind::Cluster,"target",{}});
    budget_nodes.emplace(dependency,GraphNode{dependency,NodeKind::Dependency,
        "CONTENT_EQUALS:never-match",{"root",target}});
  }
  auto operation_limited=DependencyEngine::evaluate(budget_nodes,{"root"},4096,150);
  assert(!operation_limited.complete);
  assert(operation_limited.error=="DEPENDENCY_PREDICATE_BUDGET_EXCEEDED");
  assert(operation_limited.predicate_operations==151);
  auto operation_complete=DependencyEngine::evaluate(budget_nodes,{"root"},4096,256);
  assert(operation_complete.complete);
  assert(operation_complete.predicate_operations==200);
  {
    std::map<std::string,GraphNode> temporal_nodes;
    temporal_nodes.emplace("claim-temporal",GraphNode{"claim-temporal",NodeKind::Claim,"契約期間",{}});
    temporal_nodes.emplace("valid-dependent",GraphNode{"valid-dependent",NodeKind::Cluster,"有効時の処理",{}});
    temporal_nodes.emplace("expired-dependent",GraphNode{"expired-dependent",NodeKind::Cluster,"失効後の処理",{}});
    temporal_nodes.emplace("unknown-dependent",GraphNode{"unknown-dependent",NodeKind::Cluster,"不明時の処理",{}});
    temporal_nodes.emplace("dep-valid",GraphNode{"dep-valid",NodeKind::Dependency,
        "CLAIM_TEMPORAL_STATE:VALID@2026-02-01T00:00:00Z",
        {"claim-temporal","valid-dependent"}});
    temporal_nodes.emplace("dep-expired",GraphNode{"dep-expired",NodeKind::Dependency,
        "CLAIM_TEMPORAL_STATE:EXPIRED@2026-05-01T00:00:00Z",
        {"claim-temporal","expired-dependent"}});
    temporal_nodes.emplace("dep-unknown",GraphNode{"dep-unknown",NodeKind::Dependency,
        "CLAIM_TEMPORAL_STATE:UNKNOWN@2026-02-01T00:00:00Z",
        {"claim-temporal","unknown-dependent"}});
    temporal_nodes.emplace("dep-malformed",GraphNode{"dep-malformed",NodeKind::Dependency,
        "CLAIM_TEMPORAL_STATE:INVALID@2026-02-01T00:00:00Z",
        {"claim-temporal","unknown-dependent"}});
    ClaimValidityRecord active{{"claim-temporal","2026-01-01T00:00:00Z",
        "2026-04-01T00:00:00Z","evidence",1},1};
    auto valid_state=DependencyEngine::evaluate(temporal_nodes,{"claim-temporal"},4096,65536,
        {{"claim-temporal",active}});
    assert(valid_state.complete);
    assert((valid_state.triggered_dependencies==std::vector<std::string>{"dep-valid"}));
    assert((valid_state.rejected_dependencies==std::vector<std::string>{"dep-malformed"}));
    assert(std::find(valid_state.impacted_nodes.begin(),valid_state.impacted_nodes.end(),
        "valid-dependent")!=valid_state.impacted_nodes.end());
    assert(std::find(valid_state.impacted_nodes.begin(),valid_state.impacted_nodes.end(),
        "expired-dependent")==valid_state.impacted_nodes.end());
    auto no_validity=DependencyEngine::evaluate(temporal_nodes,{"claim-temporal"},4096,65536,{});
    assert(no_validity.complete);
    assert((no_validity.triggered_dependencies==std::vector<std::string>{"dep-unknown"}));
  }
  std::cout<<"SURGE DependencyEngine tests: 11 groups passed\n";
}
