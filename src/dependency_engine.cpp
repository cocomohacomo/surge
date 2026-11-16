#include "surge/dependency_engine.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace surge {
DependencyEvaluationResult DependencyEngine::evaluate(
    const std::map<std::string, GraphNode>& nodes,
    const std::vector<std::string>& roots, std::size_t maximum_nodes) {
  DependencyEvaluationResult result;
  if (roots.empty() || maximum_nodes == 0) {
    result.error = "DEPENDENCY_EVALUATION_REQUEST_INVALID"; return result;
  }
  std::set<std::string> changed;
  for (const auto& id : roots) {
    if (!nodes.count(id)) { result.error = "DEPENDENCY_TRIGGER_ROOT_NOT_FOUND"; return result; }
    changed.insert(id);
  }
  if (changed.size() > maximum_nodes) {
    result.error = "DEPENDENCY_EVALUATION_BUDGET_EXCEEDED"; return result;
  }
  std::vector<const GraphNode*> dependencies;
  for (const auto& item : nodes) {
    const auto& dep = item.second;
    if (dep.kind != NodeKind::Dependency) continue;
    if (dep.references.size() != 2 || dep.references[0] == dep.references[1] ||
        !nodes.count(dep.references[0]) || !nodes.count(dep.references[1])) {
      result.error = "DEPENDENCY_RECORD_INVALID"; return result;
    }
    dependencies.push_back(&dep);
  }
  auto matches = [&](const GraphNode& dep, const GraphNode& prerequisite) {
    const auto& expr = dep.content;
    if (expr.empty() || expr == "ON_CHANGE") return changed.count(prerequisite.id) != 0;
    if (expr == "PRESENT") return true;
    const std::string eq = "CONTENT_EQUALS:";
    if (expr.compare(0, eq.size(), eq) == 0) return prerequisite.content == expr.substr(eq.size());
    const std::string contains = "CONTENT_CONTAINS:";
    if (expr.compare(0, contains.size(), contains) == 0) {
      const auto needle = expr.substr(contains.size());
      return !needle.empty() && prerequisite.content.find(needle) != std::string::npos;
    }
    const std::string kind = "KIND:";
    if (expr.compare(0, kind.size(), kind) == 0) {
      const auto expected = expr.substr(kind.size());
      switch (prerequisite.kind) {
        case NodeKind::Cluster: return expected == "CLUSTER";
        case NodeKind::Entity: return expected == "ENTITY";
        case NodeKind::Evidence: return expected == "EVIDENCE";
        case NodeKind::Claim: return expected == "CLAIM";
        case NodeKind::Question: return expected == "QUESTION";
        case NodeKind::QuestionAspect: return expected == "QUESTION_ASPECT";
        case NodeKind::Dependency: return expected == "DEPENDENCY";
        case NodeKind::Interest: return expected == "INTEREST";
      }
    }
    return false;
  };
  auto known = [](const std::string& expr) {
    if (expr.empty() || expr == "ON_CHANGE" || expr == "PRESENT") return true;
    const std::string eq = "CONTENT_EQUALS:";
    if (expr.compare(0, eq.size(), eq) == 0) return expr.size() > eq.size();
    const std::string contains = "CONTENT_CONTAINS:";
    if (expr.compare(0, contains.size(), contains) == 0)
      return expr.size() > contains.size();
    const std::string kind = "KIND:";
    if (expr.compare(0, kind.size(), kind) != 0) return false;
    const auto value = expr.substr(kind.size());
    return value == "CLUSTER" || value == "ENTITY" || value == "EVIDENCE" ||
      value == "CLAIM" || value == "QUESTION" || value == "QUESTION_ASPECT" ||
      value == "DEPENDENCY" || value == "INTEREST";
  };
  std::set<std::string> fired, rejected;
  for (const auto* dep : dependencies) if (!known(dep->content)) rejected.insert(dep->id);
  bool progress = true;
  while (progress) {
    progress = false;
    for (const auto* dep : dependencies) {
      if (fired.count(dep->id)) continue;
      if (!matches(*dep, nodes.at(dep->references[0]))) continue;
      fired.insert(dep->id);
      if (changed.insert(dep->references[1]).second) {
        if (changed.size() > maximum_nodes) {
          result.error = "DEPENDENCY_EVALUATION_BUDGET_EXCEEDED";
          result.impacted_nodes.assign(changed.begin(), changed.end()); return result;
        }
        progress = true;
      }
    }
  }
  result.triggered_dependencies.assign(fired.begin(), fired.end());
  result.rejected_dependencies.assign(rejected.begin(), rejected.end());
  result.impacted_nodes.assign(changed.begin(), changed.end());
  result.complete = true;
  return result;
}

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
