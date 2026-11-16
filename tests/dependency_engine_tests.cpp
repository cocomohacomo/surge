#include "surge/dependency_engine.hpp"
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
  std::cout<<"SURGE DependencyEngine tests: 9 groups passed\n";
}
