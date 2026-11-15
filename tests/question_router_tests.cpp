#include "surge/question_router.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace surge;

static SemanticDelta base_delta(bool two_clusters){
  SemanticDelta d;d.delta_id="seed";d.idempotency_key="seed";d.expected_state_version=0;
  d.nodes={{{"cluster-a",NodeKind::Cluster,"A",{}}},
           {{"evidence-a",NodeKind::Evidence,"根拠A",{}}},
           {{"evidence-b",NodeKind::Evidence,"根拠B",{}}},
           {{"claim-a",NodeKind::Claim,"主張A",{"evidence-a"}}},
           {{"claim-b",NodeKind::Claim,"主張B",{"evidence-b"}}}};
  if(two_clusters)d.nodes.push_back({{"cluster-b",NodeKind::Cluster,"B",{}}});
  d.relations={{{"about-a",RelationKind::About,"claim-a","cluster-a"}},
               {{"about-b",RelationKind::About,"claim-b",two_clusters?"cluster-b":"cluster-a"}},
               {{"contradiction-1",RelationKind::Contradicts,"claim-a","claim-b"}}};
  return d;
}
int main(){
  const auto root=std::filesystem::temp_directory_path()/"surge-question-router-tests";
  std::filesystem::remove_all(root);
  {
    ContextGraph graph((root/"multi.sgj").string());assert(graph.open());
    assert(graph.apply(base_delta(true)).committed);
    auto routed=QuestionRouter::route(graph,"contradiction-1","policy-1");
    assert(routed.committed && !routed.focus_selected && routed.interest_ids.size()==2);
    assert(graph.nodes().count(routed.question_id)==1);
    assert(graph.nodes().count(routed.question_aspect_id)==1);
    assert(!graph.focus());
    const auto version=graph.state_version();
    auto retry=QuestionRouter::route(graph,"contradiction-1","policy-1");
    assert(retry.committed && retry.duplicate && graph.state_version()==version);
  }
  {
    ContextGraph graph((root/"single.sgj").string());assert(graph.open());
    assert(graph.apply(base_delta(false)).committed);
    auto routed=QuestionRouter::route(graph,"contradiction-1","policy-1");
    assert(routed.committed && routed.focus_selected && routed.interest_ids.size()==1);
    assert(graph.focus() && graph.focus()->cluster_id=="cluster-a");
    assert(graph.focus()->interest_id==routed.interest_ids.front());
  }
  {
    ContextGraph graph((root/"invalid.sgj").string());assert(graph.open());
    assert(graph.apply(base_delta(false)).committed);
    auto invalid=QuestionRouter::route(graph,"about-a","policy-1");
    assert(!invalid.committed && invalid.error=="CONTRADICTION_RELATION_NOT_FOUND");
    auto missing=QuestionRouter::route(graph,"contradiction-1","");
    assert(!missing.committed && missing.error=="QUESTION_ROUTE_REQUEST_INVALID");
  }
  std::filesystem::remove_all(root);
  std::cout<<"SURGE QuestionRouter tests: 3 groups passed\n";
}
