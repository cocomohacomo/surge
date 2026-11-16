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
      ContextGraph graph(dependency_path);assert(graph.open());
      SemanticDelta d;d.delta_id="dependency-delta-1";d.idempotency_key="dependency-key-1";
      d.expected_state_version=0;
      d.nodes={{{"source-cluster",NodeKind::Cluster,"approved",{}}},
               {{"target-cluster",NodeKind::Cluster,"target",{}}},
               {{"dependency-good",NodeKind::Dependency,"CONTENT_EQUALS:approved",
                  {"source-cluster","target-cluster"}}},
               {{"dependency-invalid",NodeKind::Dependency,"KIND:UNKNOWN",
                  {"source-cluster","target-cluster"}}}};
      const auto result=graph.apply(d);
      assert(result.committed && !result.duplicate && result.state_version==1);
      assert(graph.dependency_evaluations().count("dependency-delta-1")==1);
      const auto evaluation_it=graph.dependency_evaluations().find("dependency-delta-1"); assert(evaluation_it!=graph.dependency_evaluations().end()); const auto& evaluation=evaluation_it->second;
      assert(evaluation.triggered.size()==1);
      assert(evaluation.triggered[0].dependency_id=="dependency-good");
      assert(evaluation.triggered[0].prerequisite_id=="source-cluster");
      assert(evaluation.triggered[0].dependent_id=="target-cluster");
      assert((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      const auto retry=graph.apply(d);
      assert(retry.committed && retry.duplicate && graph.state_version()==1);
    }
    {
      ContextGraph recovered(dependency_path);assert(recovered.open());
      assert(recovered.state_version()==1);
      assert(recovered.dependency_evaluations().count("dependency-delta-1")==1);
      const auto evaluation_it=recovered.dependency_evaluations().find("dependency-delta-1"); assert(evaluation_it!=recovered.dependency_evaluations().end()); const auto& evaluation=evaluation_it->second;
      assert(evaluation.triggered.size()==1 &&
             evaluation.triggered[0].dependency_id=="dependency-good");
      assert((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      SemanticDelta new_dependency;new_dependency.delta_id="dependency-delta-2";
      new_dependency.idempotency_key="dependency-key-2";new_dependency.expected_state_version=1;
      new_dependency.nodes={{{"dependency-on-change",NodeKind::Dependency,"ON_CHANGE",
          {"source-cluster","target-cluster"}}}};
      const auto added=recovered.apply(new_dependency);
      assert(added.committed&&added.state_version==2);
      assert(recovered.dependency_evaluations().count("dependency-delta-2")==1);
      const auto evaluation2_it=recovered.dependency_evaluations().find("dependency-delta-2"); assert(evaluation2_it!=recovered.dependency_evaluations().end()); assert(evaluation2_it->second.triggered.size()==1);
      assert(evaluation2_it->second.triggered[0].dependency_id==
          "dependency-on-change");
    }
    {
      ContextGraph recovered(dependency_path);assert(recovered.open());
      assert(recovered.state_version()==2);
      assert(recovered.dependency_evaluations().count("dependency-delta-1")==1);
      assert(recovered.dependency_evaluations().count("dependency-delta-2")==1);
      const auto evaluation_it=recovered.dependency_evaluations().find("dependency-delta-1");
      const auto evaluation2_it=recovered.dependency_evaluations().find("dependency-delta-2");
      assert(evaluation_it!=recovered.dependency_evaluations().end());
      assert(evaluation2_it!=recovered.dependency_evaluations().end());
      const auto& evaluation=evaluation_it->second;
      assert(evaluation.triggered.size()==1 &&
             evaluation.triggered[0].dependency_id=="dependency-good");
      assert((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      assert(evaluation2_it->second.triggered[0].dependency_id==
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
  {
    const auto claim_path=(root/"claim-lifecycle.sgj").string();
    {
      ContextGraph graph(claim_path);assert(graph.open());
      SemanticDelta seed;seed.delta_id="claim-seed";seed.idempotency_key="claim-seed";
      seed.expected_state_version=0;
      seed.nodes={{{"evidence-old",NodeKind::Evidence,"当初の会議記録",{}}},
                  {{"claim-old",NodeKind::Claim,"予算は100万円",{"evidence-old"}}}};
      assert(graph.apply(seed).committed);
      assert(graph.claim_status("claim-old")==ClaimStatus::Active);
      SemanticDelta correction;correction.delta_id="claim-correction";
      correction.idempotency_key="claim-correction";correction.expected_state_version=1;
      correction.nodes={{{"evidence-new",NodeKind::Evidence,"訂正後の会議記録",{}}},
                        {{"claim-new",NodeKind::Claim,"予算は120万円",{"evidence-new"}}}};
      correction.relations={{{"claim-supersedes",RelationKind::Supersedes,
                              "claim-new","claim-old"}}};
      assert(graph.apply(correction).committed);
      assert(graph.claim_status("claim-old")==ClaimStatus::Superseded);
      assert(graph.claim_status("claim-new")==ClaimStatus::Active);
      SemanticDelta invalid;invalid.delta_id="claim-double-supersede";
      invalid.idempotency_key="claim-double-supersede";invalid.expected_state_version=2;
      invalid.relations={{{"claim-supersedes-again",RelationKind::Supersedes,
                           "claim-new","claim-old"}}};
      assert(graph.apply(invalid).error=="CLAIM_NOT_ACTIVE_FOR_SUPERSESSION");
      assert(graph.state_version()==2);
      SemanticDelta retract;retract.delta_id="claim-retract";
      retract.idempotency_key="claim-retract";retract.expected_state_version=2;
      retract.nodes={{{"evidence-retract",NodeKind::Evidence,"根拠の誤りを確認",{}}}};
      retract.relations={{{"claim-retracted",RelationKind::Retracts,
                           "evidence-retract","claim-new"}}};
      assert(graph.apply(retract).committed);
      assert(graph.claim_status("claim-new")==ClaimStatus::Retracted);
      assert(graph.claim_status("claim-old")==ClaimStatus::Superseded);
    }
    {
      ContextGraph recovered(claim_path);assert(recovered.open());
      assert(recovered.state_version()==3);
      assert(recovered.claim_status("claim-old")==ClaimStatus::Superseded);
      assert(recovered.claim_status("claim-new")==ClaimStatus::Retracted);
      assert(recovered.relations().count("claim-supersedes")==1);
      assert(recovered.relations().count("claim-retracted")==1);
    }
  }
  {
    const auto temporal_path=(root/"claim-temporal-validity.sgj").string();
    {
      ContextGraph graph(temporal_path);assert(graph.open());
      SemanticDelta seed;seed.delta_id="temporal-seed";seed.idempotency_key="temporal-seed";
      seed.expected_state_version=0;
      seed.nodes={{{"temporal-evidence-1",NodeKind::Evidence,"契約書",{}}},
                  {{"temporal-claim",NodeKind::Claim,"契約は2026年1月から有効",{"temporal-evidence-1"}}}};
      seed.claim_validity_updates.push_back({"temporal-claim",
          "2026-01-01T00:00:00+09:00","2026-04-01T00:00:00+09:00",
          "temporal-evidence-1",1});
      assert(graph.apply(seed).committed && graph.state_version()==1);
      assert(graph.claim_validity_at("temporal-claim","2025-12-31T14:59:59Z")==ClaimTemporalState::NotYetValid);
      assert(graph.claim_validity_at("temporal-claim","2025-12-31T15:00:00Z")==ClaimTemporalState::Valid);
      assert(graph.claim_validity_at("temporal-claim","2026-03-31T15:00:00Z")==ClaimTemporalState::Expired);
      assert(graph.claim_validity_at("missing-claim","2026-02-01T00:00:00Z")==ClaimTemporalState::Unknown);
      SemanticDelta correction;correction.delta_id="temporal-correction";
      correction.idempotency_key="temporal-correction";correction.expected_state_version=1;
      correction.nodes={{{"temporal-evidence-2",NodeKind::Evidence,"契約開始日の訂正",{}}}};
      correction.claim_validity_updates.push_back({"temporal-claim",
          "2025-12-01T00:00:00Z","2026-04-01T00:00:00Z",
          "temporal-evidence-2",2});
      assert(graph.apply(correction).committed && graph.state_version()==2);
      assert(graph.claim_validity_at("temporal-claim","2025-12-15T00:00:00Z")==ClaimTemporalState::Valid);
      assert(graph.claim_validity_history().at("temporal-claim").size()==2);
      SemanticDelta invalid;invalid.delta_id="temporal-invalid";
      invalid.idempotency_key="temporal-invalid";invalid.expected_state_version=2;
      invalid.claim_validity_updates.push_back({"temporal-claim",
          "2026-05-01T00:00:00Z","2026-04-01T00:00:00Z",
          "temporal-evidence-2",3});
      assert(graph.apply(invalid).error=="CLAIM_VALIDITY_INTERVAL_INVALID");
      assert(graph.state_version()==2);
    }
    {
      ContextGraph recovered(temporal_path);assert(recovered.open());
      assert(recovered.state_version()==2);
      assert(recovered.claim_validity_history().at("temporal-claim").size()==2);
      const auto latest=recovered.latest_claim_validity("temporal-claim");
      assert(latest && latest->validity.revision==2);
      assert(latest->recorded_state_version==2);
      assert(graph.claim_validity_at("temporal-claim","2025-12-15T00:00:00Z")==ClaimTemporalState::Valid);
    }
  }
  std::filesystem::remove_all(root);
  std::cout<<"SURGE ContextGraph tests: 4 groups passed\n";
}
