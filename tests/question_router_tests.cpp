#include "test_support.hpp"
#include "surge/question_router.hpp"
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
  std::string scoped_question, scoped_first_aspect, scoped_second_aspect;
  {
    ContextGraph graph((root/"scoped.sgj").string()); CHECK(graph.open());
    CHECK(graph.apply(base_delta(false)).committed);
    auto first=QuestionRouter::route_scoped(graph,"contradiction-1","policy-2","scope/fiscal-year-2026");
    CHECK(first.committed && !first.duplicate);
    scoped_question=first.question_id; scoped_first_aspect=first.question_aspect_id;
    std::string error;
    CHECK(graph.set_question_aspect_status(scoped_first_aspect,QuestionAspectStatus::Resolved,graph.state_version(),error));
    SemanticDelta second; second.delta_id="second-contradiction"; second.idempotency_key=second.delta_id;
    second.expected_state_version=graph.state_version();
    second.relations.push_back({{"contradiction-2",RelationKind::Contradicts,"claim-b","claim-a"}});
    CHECK(graph.apply(second).committed);
    auto reopened=QuestionRouter::route_scoped(graph,"contradiction-2","policy-2","scope/fiscal-year-2026");
    CHECK(reopened.committed && !reopened.duplicate);
    CHECK(reopened.question_id==scoped_question && reopened.question_aspect_id!=scoped_first_aspect);
    scoped_second_aspect=reopened.question_aspect_id;
    CHECK(graph.question_aspect_status(scoped_first_aspect)==QuestionAspectStatus::Resolved);
    CHECK(graph.question_aspect_status(scoped_second_aspect)==QuestionAspectStatus::Open);
    auto duplicate=QuestionRouter::route_scoped(graph,"contradiction-2","policy-2","scope/fiscal-year-2026");
    CHECK(duplicate.committed && duplicate.duplicate);
    auto other_scope=QuestionRouter::route_scoped(graph,"contradiction-2","policy-2","scope/fiscal-year-2027");
    CHECK(other_scope.committed && other_scope.question_id!=scoped_question);
  }
  {
    ContextGraph recovered((root/"scoped.sgj").string()); CHECK(recovered.open());
    CHECK(recovered.nodes().count(scoped_question)==1);
    CHECK(recovered.question_aspect_status(scoped_first_aspect)==QuestionAspectStatus::Resolved);
    CHECK(recovered.question_aspect_status(scoped_second_aspect)==QuestionAspectStatus::Open);
  }
  {
    ContextGraph graph((root/"multi.sgj").string());CHECK(graph.open());
    CHECK(graph.apply(base_delta(true)).committed);
    auto routed=QuestionRouter::route(graph,"contradiction-1","policy-1");
    CHECK(routed.committed && !routed.focus_selected && routed.interest_ids.size()==2);
    CHECK(graph.nodes().count(routed.question_id)==1);
    CHECK(graph.nodes().count(routed.question_aspect_id)==1);
    CHECK(!graph.focus());
    const auto version=graph.state_version();
    auto retry=QuestionRouter::route(graph,"contradiction-1","policy-1");
    CHECK(retry.committed && retry.duplicate && graph.state_version()==version);
  }
  {
    ContextGraph graph((root/"single.sgj").string());CHECK(graph.open());
    CHECK(graph.apply(base_delta(false)).committed);
    auto routed=QuestionRouter::route(graph,"contradiction-1","policy-1");
    CHECK(routed.committed && routed.focus_selected && routed.interest_ids.size()==1);
    CHECK(graph.focus() && graph.focus()->cluster_id=="cluster-a");
    CHECK(graph.focus()->interest_id==routed.interest_ids.front());
  }
  {
    ContextGraph graph((root/"invalid.sgj").string());CHECK(graph.open());
    CHECK(graph.apply(base_delta(false)).committed);
    auto invalid=QuestionRouter::route(graph,"about-a","policy-1");
    CHECK(!invalid.committed && invalid.error=="CONTRADICTION_RELATION_NOT_FOUND");
    auto missing=QuestionRouter::route(graph,"contradiction-1","");
    CHECK(!missing.committed && missing.error=="QUESTION_ROUTE_REQUEST_INVALID");
  }
  std::filesystem::remove_all(root);
  std::cout<<"SURGE QuestionRouter tests: 4 groups passed\n";
}
