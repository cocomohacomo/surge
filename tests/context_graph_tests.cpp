#include "surge/context_graph.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace surge;
int main(){
  const auto root=std::filesystem::temp_directory_path()/"surge-context-graph-tests";

  {
    const auto qpath=(root/"question-aspect-status.sgj").string();
    {
      ContextGraph graph(qpath);assert(graph.open());
      SemanticDelta d;d.delta_id="qa-seed";d.idempotency_key="qa-seed";d.expected_state_version=0;
      d.nodes={{{"question-x",NodeKind::Question,"確認事項",{}}},
               {{"aspect-x",NodeKind::QuestionAspect,"期限の確認",{"question-x"}}}};
      assert(graph.apply(d).committed && graph.state_version()==1);
      std::string error;
      assert(graph.question_aspect_status("aspect-x")==QuestionAspectStatus::Open);
      assert(graph.set_question_aspect_status("aspect-x",QuestionAspectStatus::Resolved,1,error));
      assert(graph.state_version()==2);
      assert(graph.question_aspect_status("aspect-x")==QuestionAspectStatus::Resolved);
      assert(!graph.set_question_aspect_status("aspect-x",QuestionAspectStatus::Deferred,1,error));
      assert(error=="STATE_VERSION_CONFLICT");
    }
    {
      ContextGraph recovered(qpath);assert(recovered.open());
      assert(recovered.state_version()==2);
      assert(recovered.question_aspect_status("aspect-x")==QuestionAspectStatus::Resolved);
      std::string error;
      assert(recovered.set_question_aspect_status("aspect-x",QuestionAspectStatus::Deferred,2,error));
    }
    {
      ContextGraph recovered(qpath);assert(recovered.open());
      assert(recovered.state_version()==3);
      assert(recovered.question_aspect_status("aspect-x")==QuestionAspectStatus::Deferred);
    }
  }
  {
    const auto dependency_path=(root/"dependency-evaluation.sgj").string();
    {
      ContextGraph graph(dependency_path);assert(graph.open()); std::cerr<<"dep-test: fresh-open\\n";
      SemanticDelta d;d.delta_id="dependency-delta-1";d.idempotency_key="dependency-key-1";
      d.expected_state_version=0;
      d.nodes={{{"source-cluster",NodeKind::Cluster,"approved",{}}},
               {{"target-cluster",NodeKind::Cluster,"target",{}}},
               {{"dependency-good",NodeKind::Dependency,"CONTENT_EQUALS:approved",
                  {"source-cluster","target-cluster"}}},
               {{"dependency-invalid",NodeKind::Dependency,"KIND:UNKNOWN",
                  {"source-cluster","target-cluster"}}}};
      const auto result=graph.apply(d); std::cerr<<"dep-test: first-apply\\n";
      assert(result.committed && !result.duplicate && result.state_version==1);
      assert(graph.dependency_evaluations().count("dependency-delta-1")==1);
      const auto& evaluation=graph.dependency_evaluations().at("dependency-delta-1");
      assert(evaluation.triggered.size()==1);
      assert(evaluation.triggered[0].dependency_id=="dependency-good");
      assert(evaluation.triggered[0].prerequisite_id=="source-cluster");
      assert(evaluation.triggered[0].dependent_id=="target-cluster");
      assert((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      const auto retry=graph.apply(d);
      assert(retry.committed && retry.duplicate && graph.state_version()==1);
    }
    {
      ContextGraph recovered(dependency_path);assert(recovered.open()); std::cerr<<"dep-test: recovered-open\\n";
      assert(recovered.state_version()==1);
      assert(recovered.dependency_evaluations().count("dependency-delta-1")==1);
      const auto& evaluation=recovered.dependency_evaluations().at("dependency-delta-1");
      assert(evaluation.triggered.size()==1 &&
             evaluation.triggered[0].dependency_id=="dependency-good");
      assert((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      SemanticDelta new_dependency;new_dependency.delta_id="dependency-delta-2";
      new_dependency.idempotency_key="dependency-key-2";new_dependency.expected_state_version=1;
      new_dependency.nodes={{{"dependency-on-change",NodeKind::Dependency,"ON_CHANGE",
          {"source-cluster","target-cluster"}}}};
      const auto added=recovered.apply(new_dependency); std::cerr<<"dep-test: second-apply\\n";
      assert(added.committed&&added.state_version==2);
      assert(recovered.dependency_evaluations().count("dependency-delta-2")==1);
      assert(recovered.dependency_evaluations().at("dependency-delta-2").triggered.size()==1);
      assert(recovered.dependency_evaluations().at("dependency-delta-2").triggered[0].dependency_id==
          "dependency-on-change");
    }
    {
      ContextGraph recovered(dependency_path);assert(recovered.open());
      assert(recovered.state_version()==2);
      assert(recovered.dependency_evaluations().count("dependency-delta-1")==1);
      assert(recovered.dependency_evaluations().count("dependency-delta-2")==1);
      const auto& evaluation=recovered.dependency_evaluations().at("dependency-delta-1");
      assert(evaluation.triggered.size()==1 &&
             evaluation.triggered[0].dependency_id=="dependency-good");
      assert((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      assert(recovered.dependency_evaluations().at("dependency-delta-2").triggered[0].dependency_id==
          "dependency-on-change");
    }
  }
  std::filesystem::remove_all(root);const auto path=(root/"context.sgj").string();
  {
    ContextGraph graph(path);assert(graph.open());
    SemanticDelta d;d.delta_id="d1";d.idempotency_key="k1";d.expected_state_version=0;
    d.nodes={{{"cluster-1",NodeKind::Cluster,"予算案",{} }},
             {{"evidence-1",NodeKind::Evidence,"会議発言",{} }},
             {{"claim-1",NodeKind::Claim,"予算を増額する",{"evidence-1"}}},
             {{"question-1",NodeKind::Question,"予算は増額可能か",{} }},
             {{"aspect-1",NodeKind::QuestionAspect,"財源の有無",{"question-1"}}},
             {{"interest-1",NodeKind::Interest,"財源を確認",{"aspect-1","cluster-1"}}}};
    d.relations={{{"rel-1",RelationKind::About,"claim-1","cluster-1"}}};
    d.focus=SetFocus{{"cluster-1","interest-1"}};
    auto result=graph.apply(d);assert(result.committed && result.state_version==1);
    assert(graph.nodes().size()==6 && graph.relations().size()==1);
    assert(graph.focus() && graph.focus()->interest_id=="interest-1");
    auto retry=graph.apply(d);assert(retry.committed && retry.duplicate && graph.state_version()==1);
    auto conflict=d;conflict.nodes[0].node.content="別の内容";
    assert(graph.apply(conflict).error=="IDEMPOTENCY_KEY_REUSED");
    SemanticDelta invalid;invalid.delta_id="d2";invalid.idempotency_key="k2";invalid.expected_state_version=1;
    invalid.relations={{{"bad-rel",RelationKind::Contradicts,"claim-1","cluster-1"}}};
    assert(graph.apply(invalid).error=="CLAIM_RELATION_TYPE_INVALID");
    assert(graph.state_version()==1);
    SemanticDelta stale;stale.delta_id="d3";stale.idempotency_key="k3";stale.expected_state_version=0;
    stale.nodes={{{"cluster-2",NodeKind::Cluster,"別議題",{}}}};
    assert(graph.apply(stale).error=="STATE_VERSION_CONFLICT");
  }
  {
    ContextGraph recovered(path);assert(recovered.open());
    assert(recovered.state_version()==1);
    assert(recovered.nodes().size()==6 && recovered.relations().size()==1);
    assert(recovered.focus() && recovered.focus()->cluster_id=="cluster-1");
    assert(recovered.focus_history().size()==1);
    SemanticDelta original;original.delta_id="d1";original.idempotency_key="k1";original.expected_state_version=0;
    original.nodes={{{"cluster-1",NodeKind::Cluster,"予算案",{}}},{{"evidence-1",NodeKind::Evidence,"会議発言",{}}},{{"claim-1",NodeKind::Claim,"予算を増額する",{"evidence-1"}}},{{"question-1",NodeKind::Question,"予算は増額可能か",{}}},{{"aspect-1",NodeKind::QuestionAspect,"財源の有無",{"question-1"}}},{{"interest-1",NodeKind::Interest,"財源を確認",{"aspect-1","cluster-1"}}}};
    original.relations={{{"rel-1",RelationKind::About,"claim-1","cluster-1"}}};
    original.focus=SetFocus{{"cluster-1","interest-1"}};
    auto replay_retry=recovered.apply(original);assert(replay_retry.committed && replay_retry.duplicate);
    SemanticDelta d;d.delta_id="d2";d.idempotency_key="k2";d.expected_state_version=1;
    d.nodes={{{"cluster-2",NodeKind::Cluster,"派生議題",{}}}};
    auto result=recovered.apply(d);assert(result.committed && result.state_version==2);
  }
  {
    ContextGraph recovered(path);assert(recovered.open());
    assert(recovered.state_version()==2);
    assert(recovered.nodes().count("cluster-2")==1);
    assert(recovered.nodes().count("cluster-1")==1);
  }
  std::filesystem::remove_all(root);
  std::cout<<"SURGE ContextGraph tests: 2 groups passed\n";
}
