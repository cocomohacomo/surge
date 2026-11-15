#include "surge/context_graph.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace surge;
int main(){
  const auto root=std::filesystem::temp_directory_path()/"surge-context-graph-tests";
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
