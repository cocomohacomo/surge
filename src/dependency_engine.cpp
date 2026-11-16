#include "surge/dependency_engine.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace surge {

DependencyPredicate DependencyEngine::parse_predicate(const std::string& expression) {
  DependencyPredicate result;
  if (expression.empty() || expression == "ON_CHANGE") {
    result.kind = DependencyPredicateKind::OnChange;
    return result;
  }
  if (expression == "PRESENT") {
    result.kind = DependencyPredicateKind::Present;
    return result;
  }
  const std::string equals = "CONTENT_EQUALS:";
  if (expression.compare(0, equals.size(), equals) == 0) {
    result.operand = expression.substr(equals.size());
    if (result.operand.empty()) { result.error = "DEPENDENCY_PREDICATE_OPERAND_EMPTY"; return result; }
    result.kind = DependencyPredicateKind::ContentEquals;
    return result;
  }
  const std::string contains = "CONTENT_CONTAINS:";
  if (expression.compare(0, contains.size(), contains) == 0) {
    result.operand = expression.substr(contains.size());
    if (result.operand.empty()) { result.error = "DEPENDENCY_PREDICATE_OPERAND_EMPTY"; return result; }
    result.kind = DependencyPredicateKind::ContentContains;
    return result;
  }
  const std::string kind = "KIND:";
  if (expression.compare(0, kind.size(), kind) == 0) {
    result.expected_node_kind = expression.substr(kind.size());
    static const std::set<std::string> allowed = {
      "CLUSTER", "ENTITY", "EVIDENCE", "CLAIM", "QUESTION",
      "QUESTION_ASPECT", "DEPENDENCY", "INTEREST"
    };
    if (!allowed.count(result.expected_node_kind)) {
      result.error = "DEPENDENCY_PREDICATE_NODE_KIND_INVALID";
      return result;
    }
    result.kind = DependencyPredicateKind::Kind;
    return result;
  }
  result.error = "DEPENDENCY_PREDICATE_UNKNOWN";
  return result;
}

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

  auto matches = [&](const DependencyPredicate& predicate,
                     const GraphNode& prerequisite) {
    switch (predicate.kind) {
      case DependencyPredicateKind::OnChange:
        return changed.count(prerequisite.id) != 0;
      case DependencyPredicateKind::Present:
        return true;
      case DependencyPredicateKind::ContentEquals:
        return prerequisite.content == predicate.operand;
      case DependencyPredicateKind::ContentContains:
        return prerequisite.content.find(predicate.operand) != std::string::npos;
      case DependencyPredicateKind::Kind:
        switch (prerequisite.kind) {
          case NodeKind::Cluster: return predicate.expected_node_kind == "CLUSTER";
          case NodeKind::Entity: return predicate.expected_node_kind == "ENTITY";
          case NodeKind::Evidence: return predicate.expected_node_kind == "EVIDENCE";
          case NodeKind::Claim: return predicate.expected_node_kind == "CLAIM";
          case NodeKind::Question: return predicate.expected_node_kind == "QUESTION";
          case NodeKind::QuestionAspect: return predicate.expected_node_kind == "QUESTION_ASPECT";
          case NodeKind::Dependency: return predicate.expected_node_kind == "DEPENDENCY";
          case NodeKind::Interest: return predicate.expected_node_kind == "INTEREST";
        }
        return false;
      case DependencyPredicateKind::Invalid:
        return false;
    }
    return false;
  };
  std::set<std::string> fired, rejected;
  std::map<std::string, DependencyPredicate> parsed;
  for (const auto* dep : dependencies) {
    auto predicate = parse_predicate(dep->content);
    if (!predicate.valid()) rejected.insert(dep->id);
    parsed.emplace(dep->id, std::move(predicate));
  }
  bool progress = true;
  while (progress) {
    progress = false;
    for (const auto* dep : dependencies) {
      if (fired.count(dep->id)) continue;
      const auto prerequisite = nodes.find(dep->references[0]);
      if (prerequisite == nodes.end()) {
        result.error = "DEPENDENCY_PREREQUISITE_NOT_FOUND"; return result;
      }
      const auto predicate = parsed.find(dep->id);
      if (predicate == parsed.end() || !predicate->second.valid() ||
          !matches(predicate->second, prerequisite->second)) continue;
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

FixpointEvaluationResult DependencyEngine::evaluate_fixpoint(
    const std::map<std::string, GraphNode>& nodes,
    const std::vector<std::string>& roots,
    std::size_t maximum_nodes,
    std::size_t maximum_iterations,
    std::size_t maximum_evaluations) {
  FixpointEvaluationResult result;
  if (roots.empty() || maximum_nodes == 0 || maximum_iterations == 0 ||
      maximum_evaluations == 0) {
    result.error = "DEPENDENCY_FIXPOINT_REQUEST_INVALID";
    return result;
  }

  std::set<std::string> changed;
  for (const auto& id : roots) {
    if (!nodes.count(id)) {
      result.status = FixpointStatus::Invalid;
      result.error = "DEPENDENCY_TRIGGER_ROOT_NOT_FOUND";
      return result;
    }
    changed.insert(id);
  }

  std::vector<const GraphNode*> dependencies;
  std::map<std::string, DependencyPredicate> parsed;
  for (const auto& item : nodes) {
    const auto& dep = item.second;
    if (dep.kind != NodeKind::Dependency) continue;
    if (dep.references.size() != 2 || dep.references[0] == dep.references[1] ||
        !nodes.count(dep.references[0]) || !nodes.count(dep.references[1])) {
      result.status = FixpointStatus::Invalid;
      result.error = "DEPENDENCY_RECORD_INVALID";
      return result;
    }
    dependencies.push_back(&dep);
    auto predicate = parse_predicate(dep.content);
    if (!predicate.valid()) result.rejected_dependencies.push_back(dep.id);
    parsed.emplace(dep.id, std::move(predicate));
  }

  std::set<std::string> fired;
  auto matches = [&](const DependencyPredicate& predicate,
                     const GraphNode& prerequisite) {
    switch (predicate.kind) {
      case DependencyPredicateKind::OnChange:
        return changed.count(prerequisite.id) != 0;
      case DependencyPredicateKind::Present:
        return true;
      case DependencyPredicateKind::ContentEquals:
        return prerequisite.content == predicate.operand;
      case DependencyPredicateKind::ContentContains:
        return prerequisite.content.find(predicate.operand) != std::string::npos;
      case DependencyPredicateKind::Kind:
        switch (prerequisite.kind) {
          case NodeKind::Cluster: return predicate.expected_node_kind == "CLUSTER";
          case NodeKind::Entity: return predicate.expected_node_kind == "ENTITY";
          case NodeKind::Evidence: return predicate.expected_node_kind == "EVIDENCE";
          case NodeKind::Claim: return predicate.expected_node_kind == "CLAIM";
          case NodeKind::Question: return predicate.expected_node_kind == "QUESTION";
          case NodeKind::QuestionAspect: return predicate.expected_node_kind == "QUESTION_ASPECT";
          case NodeKind::Dependency: return predicate.expected_node_kind == "DEPENDENCY";
          case NodeKind::Interest: return predicate.expected_node_kind == "INTEREST";
        }
        return false;
      case DependencyPredicateKind::Invalid:
        return false;
    }
    return false;
  };

  // Level-triggered predicates can activate dependencies even when their
  // prerequisite is not one of the caller's changed roots. Include those
  // prerequisites when computing the potential impact and SCC closure.
  std::vector<std::string> topology_roots = roots;
  for (const auto* dep : dependencies) {
    const auto predicate = parsed.find(dep->id);
    if (predicate == parsed.end() || !predicate->second.valid() ||
        predicate->second.kind == DependencyPredicateKind::OnChange) continue;
    const auto prerequisite = nodes.find(dep->references[0]);
    if (prerequisite != nodes.end() &&
        matches(predicate->second, prerequisite->second)) {
      topology_roots.push_back(prerequisite->first);
    }
  }
  const auto topology = compute(nodes, topology_roots, maximum_nodes);
  if (!topology.complete) {
    result.status = topology.error == "IMPACT_ROOT_NOT_FOUND"
        ? FixpointStatus::Invalid : FixpointStatus::Deferred;
    result.error = topology.error;
    result.impacted_nodes = topology.impacted_nodes;
    return result;
  }
  result.cyclic_components = topology.cyclic_components;

  for (;;) {
    if (result.iterations >= maximum_iterations) {
      result.status = FixpointStatus::Deferred;
      result.error = "DEPENDENCY_FIXPOINT_ITERATION_BUDGET_EXCEEDED";
      break;
    }
    ++result.iterations;
    bool changed_this_round = false;
    for (const auto* dep : dependencies) {
      if (fired.count(dep->id)) continue;
      const auto predicate = parsed.find(dep->id);
      if (predicate == parsed.end() || !predicate->second.valid()) continue;
      if (result.evaluation_cost >= maximum_evaluations) {
        result.status = FixpointStatus::Deferred;
        result.error = "DEPENDENCY_FIXPOINT_EVALUATION_BUDGET_EXCEEDED";
        result.impacted_nodes.assign(changed.begin(), changed.end());
        result.triggered_dependencies.assign(fired.begin(), fired.end());
        return result;
      }
      ++result.evaluation_cost;
      const auto prerequisite = nodes.find(dep->references[0]);
      if (prerequisite == nodes.end()) {
        result.status = FixpointStatus::Invalid;
        result.error = "DEPENDENCY_PREREQUISITE_NOT_FOUND";
        return result;
      }
      if (!matches(predicate->second, prerequisite->second)) continue;
      fired.insert(dep->id);
      const auto inserted = changed.insert(dep->references[1]).second;
      if (inserted) {
        if (changed.size() > maximum_nodes) {
          result.status = FixpointStatus::Deferred;
          result.error = "DEPENDENCY_FIXPOINT_NODE_BUDGET_EXCEEDED";
          result.impacted_nodes.assign(changed.begin(), changed.end());
          result.triggered_dependencies.assign(fired.begin(), fired.end());
          return result;
        }
        changed_this_round = true;
      }
    }
    if (!changed_this_round) {
      result.status = FixpointStatus::Converged;
      result.error.clear();
      break;
    }
  }

  result.triggered_dependencies.assign(fired.begin(), fired.end());
  result.impacted_nodes.assign(changed.begin(), changed.end());
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
