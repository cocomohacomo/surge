#include "surge/dependency_engine.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace surge {
ImpactResult DependencyEngine::compute(const std::map<std::string, GraphNode>& nodes,
                                        const std::vector<std::string>& changed,
                                        std::size_t maximum_nodes) {
  ImpactResult result;
  if(changed.empty() || maximum_nodes==0){result.error="IMPACT_REQUEST_INVALID";return result;}
  std::map<std::string,std::vector<std::string>> adjacency;
  for(const auto& item:nodes){
    const auto& node=item.second;
    if(node.kind!=NodeKind::Dependency)continue;
    if(node.references.size()!=2 || node.references[0]==node.references[1] ||
       !nodes.count(node.references[0]) || !nodes.count(node.references[1])){
      result.error="DEPENDENCY_RECORD_INVALID";return result;
    }
    adjacency[node.references[0]].push_back(node.references[1]);
  }
  for(auto& item:adjacency){
    auto& targets=item.second;
    std::sort(targets.begin(),targets.end());
    targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
  }
  std::set<std::string> impacted;
  std::vector<std::string> work;
  for(const auto& id:changed){
    if(!nodes.count(id)){result.error="IMPACT_ROOT_NOT_FOUND";return result;}
    if(impacted.insert(id).second)work.push_back(id);
  }
  if(impacted.size()>maximum_nodes){
    result.impacted_nodes.assign(impacted.begin(),impacted.end());
    result.error="IMPACT_BUDGET_EXCEEDED";return result;
  }
  for(std::size_t i=0;i<work.size();++i){
    const auto found=adjacency.find(work[i]);
    if(found==adjacency.end())continue;
    for(const auto& target:found->second){
      if(impacted.insert(target).second){
        if(impacted.size()>maximum_nodes){
          result.impacted_nodes.assign(impacted.begin(),impacted.end());
          result.error="IMPACT_BUDGET_EXCEEDED";return result;
        }
        work.push_back(target);
      }
    }
  }
  result.impacted_nodes.assign(impacted.begin(),impacted.end());

  std::map<std::string,int> index,low;
  std::vector<std::string> stack;
  std::set<std::string> on_stack;
  int next_index=0;
  std::function<void(const std::string&)> visit=[&](const std::string& v){
    index[v]=next_index;low[v]=next_index;++next_index;
    stack.push_back(v);on_stack.insert(v);
    const auto it=adjacency.find(v);
    if(it!=adjacency.end()){
      for(const auto& w:it->second){
        if(!impacted.count(w))continue;
        if(!index.count(w)){visit(w);low[v]=std::min(low[v],low[w]);}
        else if(on_stack.count(w))low[v]=std::min(low[v],index[w]);
      }
    }
    if(low[v]==index[v]){
      std::vector<std::string> component;
      for(;;){
        const auto w=stack.back();stack.pop_back();on_stack.erase(w);component.push_back(w);
        if(w==v)break;
      }
      std::sort(component.begin(),component.end());
      bool cyclic=component.size()>1;
      if(!cyclic && it!=adjacency.end())
        cyclic=std::find(it->second.begin(),it->second.end(),v)!=it->second.end();
      if(cyclic)result.cyclic_components.push_back(std::move(component));
    }
  };
  for(const auto& id:result.impacted_nodes)if(!index.count(id))visit(id);
  std::sort(result.cyclic_components.begin(),result.cyclic_components.end());
  result.complete=true;
  return result;
}
}  // namespace surge
