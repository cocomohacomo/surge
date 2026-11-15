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
  std::cout<<"SURGE DependencyEngine tests: 6 groups passed\n";
}
