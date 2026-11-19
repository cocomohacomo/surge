#include "test_support.hpp"
#include "surge/context_graph.hpp"
#include "surge/snapshot_domains.hpp"
#include "surge/event_journal.hpp"
#include "surge/snapshot_domains.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace surge;
int main(){
  const auto root=std::filesystem::temp_directory_path()/"surge-context-graph-tests";
  {
    const auto lifecycle_path=(root/"cluster-lifecycle.sgj").string();
    {
      ContextGraph graph(lifecycle_path);CHECK(graph.open());
      SemanticDelta dormant;dormant.delta_id="cluster-dormant";dormant.idempotency_key="cluster-dormant";
      dormant.expected_state_version=0;
      dormant.nodes={{{"lifecycle-cluster",NodeKind::Cluster,"retained topic",{}}},
        {{"lifecycle-dormant",NodeKind::ClusterLifecycle,
          "DORMANT 1 \"2026-11-20T00:00:00Z\" \"low retention priority\" \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
          {"lifecycle-cluster"}}}};
      dormant.relations={{{"lifecycle-dormant-membership",RelationKind::Contains,
          "lifecycle-cluster","lifecycle-dormant"}}};
      CHECK(graph.apply(dormant).committed);
      ClusterLifecycleRecord invalid_builder_record;
      invalid_builder_record.cluster_id="lifecycle-cluster";
      invalid_builder_record.state=ClusterLifecycleState::Active;
      invalid_builder_record.revision=1;
      invalid_builder_record.recorded_at="2026-11-20T00:00:00Z";
      invalid_builder_record.reason="invalid same-state transition";
      invalid_builder_record.provenance_digest=std::string(64,'a');
      SemanticDelta invalid_builder_delta;
      std::string lifecycle_error;
      CHECK(!graph.append_cluster_lifecycle(invalid_builder_delta,invalid_builder_record,
          "invalid-builder-node","invalid-builder-edge",lifecycle_error));
      CHECK(invalid_builder_delta.nodes.empty()&&invalid_builder_delta.relations.empty());
      auto state=graph.cluster_lifecycle("lifecycle-cluster");
      CHECK(state && state->state==ClusterLifecycleState::Dormant && state->revision==1);

      SemanticDelta archived;archived.delta_id="cluster-archive";archived.idempotency_key="cluster-archive";
      archived.expected_state_version=1;
      archived.nodes={{{"lifecycle-archived",NodeKind::ClusterLifecycle,
          "ARCHIVED 2 \"2026-11-20T00:01:00Z\" \"outside active focus\" \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"",
          {"lifecycle-cluster"}}}};
      archived.relations={{{"lifecycle-archived-membership",RelationKind::Contains,
          "lifecycle-cluster","lifecycle-archived"}}};
      CHECK(graph.apply(archived).committed);
      state=graph.cluster_lifecycle("lifecycle-cluster");
      CHECK(state && state->state==ClusterLifecycleState::Archived && state->revision==2);

      SemanticDelta tombstoned;tombstoned.delta_id="cluster-tombstone";tombstoned.idempotency_key="cluster-tombstone";
      tombstoned.expected_state_version=2;
      tombstoned.nodes={{{"lifecycle-tombstone",NodeKind::ClusterLifecycle,
          "TOMBSTONED 3 \"2026-11-20T00:02:00Z\" \"retention policy approved\" \"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"",
          {"lifecycle-cluster"}}}};
      tombstoned.relations={{{"lifecycle-tombstone-membership",RelationKind::Contains,
          "lifecycle-cluster","lifecycle-tombstone"}}};
      CHECK(graph.apply(tombstoned).committed);
      CHECK(graph.nodes().count("lifecycle-cluster")==1); // Tombstone is metadata, not physical deletion.
      CHECK(graph.cluster_lifecycle("lifecycle-cluster")->state==ClusterLifecycleState::Tombstoned);

      SemanticDelta invalid;invalid.delta_id="cluster-resurrect";invalid.idempotency_key="cluster-resurrect";
      invalid.expected_state_version=3;
      invalid.nodes={{{"lifecycle-invalid-reactivation",NodeKind::ClusterLifecycle,
          "ACTIVE 4 \"2026-11-20T00:03:00Z\" \"invalid resurrection\" \"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\"",
          {"lifecycle-cluster"}}}};
      invalid.relations={{{"lifecycle-invalid-membership",RelationKind::Contains,
          "lifecycle-cluster","lifecycle-invalid-reactivation"}}};
      const auto rejected=graph.apply(invalid);
      CHECK(!rejected.committed && rejected.error=="CLUSTER_LIFECYCLE_TRANSITION_INVALID");
      CHECK(graph.state_version()==3);

      std::string snapshot_payload, snapshot_error;
      ContextGraphSnapshotDomain source_domain(graph);
      CHECK(source_domain.capture(&snapshot_payload, &snapshot_error));
      ContextGraph restored_graph((root/"cluster-lifecycle-restored.sgj").string());
      ContextGraphSnapshotDomain restored_domain(restored_graph);
      auto prepared_restore=restored_domain.prepare_restore(snapshot_payload,&snapshot_error);
      CHECK(prepared_restore!=nullptr);
      prepared_restore->commit();
      const auto snapshot_state=restored_graph.cluster_lifecycle("lifecycle-cluster");
      CHECK(snapshot_state && snapshot_state->state==ClusterLifecycleState::Tombstoned && snapshot_state->revision==3);

      SemanticDelta bad_timestamp;bad_timestamp.delta_id="cluster-bad-time";bad_timestamp.idempotency_key="cluster-bad-time";
      bad_timestamp.expected_state_version=3;
      bad_timestamp.nodes={{{"lifecycle-bad-time",NodeKind::ClusterLifecycle,
          "ACTIVE 4 \"not-a-time\" \"invalid timestamp\" \"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\"",
          {"lifecycle-cluster"}}}};
      bad_timestamp.relations={{{"lifecycle-bad-time-membership",RelationKind::Contains,
          "lifecycle-cluster","lifecycle-bad-time"}}};
      const auto bad_time_result=graph.apply(bad_timestamp);
      CHECK(!bad_time_result.committed && bad_time_result.error=="CLUSTER_LIFECYCLE_RECORD_INVALID");
      CHECK(graph.state_version()==3);

      SemanticDelta missing_membership;missing_membership.delta_id="cluster-missing-membership";
      missing_membership.idempotency_key="cluster-missing-membership";missing_membership.expected_state_version=3;
      missing_membership.nodes={{{"lifecycle-orphan-cluster",NodeKind::Cluster,"orphan parent",{}}},
        {{"lifecycle-orphan",NodeKind::ClusterLifecycle,
          "DORMANT 1 \"2026-11-20T00:03:00Z\" \"orphan metadata\" \"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\"",
          {"lifecycle-orphan-cluster"}}}};
      const auto orphan_result=graph.apply(missing_membership);
      CHECK(!orphan_result.committed && orphan_result.error=="CLUSTER_LIFECYCLE_MEMBERSHIP_REQUIRED");
      CHECK(graph.state_version()==3);
    }
    {
      ContextGraph recovered(lifecycle_path);CHECK(recovered.open());
      const auto state=recovered.cluster_lifecycle("lifecycle-cluster");
      CHECK(state && state->state==ClusterLifecycleState::Tombstoned && state->revision==3);
      CHECK(recovered.nodes().count("lifecycle-cluster")==1);
    }
  }

  {
    const auto qpath=(root/"question-aspect-status.sgj").string();
    {
      ContextGraph graph(qpath);CHECK(graph.open());
      SemanticDelta d;d.delta_id="qa-seed";d.idempotency_key="qa-seed";d.expected_state_version=0;
      d.nodes={{{"question-x",NodeKind::Question,"確認事項",{}}},
               {{"aspect-x",NodeKind::QuestionAspect,"期限の確認",{"question-x"}}}};
      CHECK(graph.apply(d).committed && graph.state_version()==1);
      std::string error;
      CHECK(graph.question_aspect_status("aspect-x")==QuestionAspectStatus::Open);
      CHECK(graph.set_question_aspect_status("aspect-x",QuestionAspectStatus::Resolved,1,error));
      CHECK(graph.state_version()==2);
      CHECK(graph.question_aspect_status("aspect-x")==QuestionAspectStatus::Resolved);
      CHECK(!graph.set_question_aspect_status("aspect-x",QuestionAspectStatus::Deferred,1,error));
      CHECK(error=="STATE_VERSION_CONFLICT");
    }
    {
      ContextGraph recovered(qpath);CHECK(recovered.open());
      CHECK(recovered.state_version()==2);
      CHECK(recovered.question_aspect_status("aspect-x")==QuestionAspectStatus::Resolved);
      std::string error;
      CHECK(recovered.set_question_aspect_status("aspect-x",QuestionAspectStatus::Deferred,2,error));
    }
    {
      ContextGraph recovered(qpath);CHECK(recovered.open());
      CHECK(recovered.state_version()==3);
      CHECK(recovered.question_aspect_status("aspect-x")==QuestionAspectStatus::Deferred);
    }
  }
  {
    const auto dependency_path=(root/"dependency-evaluation.sgj").string();
    {
      ContextGraph graph(dependency_path);CHECK(graph.open());
      SemanticDelta d;d.delta_id="dependency-delta-1";d.idempotency_key="dependency-key-1";
      d.expected_state_version=0;
      d.nodes={{{"source-cluster",NodeKind::Cluster,"approved",{}}},
               {{"target-cluster",NodeKind::Cluster,"target",{}}},
               {{"dependency-good",NodeKind::Dependency,"CONTENT_EQUALS:approved",
                  {"source-cluster","target-cluster"}}},
               {{"dependency-invalid",NodeKind::Dependency,"KIND:UNKNOWN",
                  {"source-cluster","target-cluster"}}}};
      const auto result=graph.apply(d);
      CHECK(result.committed && !result.duplicate && result.state_version==1);
      CHECK(graph.dependency_evaluations().count("dependency-delta-1")==1);
      const auto evaluation_it=graph.dependency_evaluations().find("dependency-delta-1"); CHECK(evaluation_it!=graph.dependency_evaluations().end()); const auto& evaluation=evaluation_it->second;
      CHECK(evaluation.triggered.size()==1);
      CHECK(evaluation.triggered[0].dependency_id=="dependency-good");
      CHECK(evaluation.triggered[0].prerequisite_id=="source-cluster");
      CHECK(evaluation.triggered[0].dependent_id=="target-cluster");
      CHECK((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      const auto retry=graph.apply(d);
      CHECK(retry.committed && retry.duplicate && graph.state_version()==1);
    }
    {
      ContextGraph recovered(dependency_path);CHECK(recovered.open());
      CHECK(recovered.state_version()==1);
      CHECK(recovered.dependency_evaluations().count("dependency-delta-1")==1);
      const auto evaluation_it=recovered.dependency_evaluations().find("dependency-delta-1"); CHECK(evaluation_it!=recovered.dependency_evaluations().end()); const auto& evaluation=evaluation_it->second;
      CHECK(evaluation.triggered.size()==1 &&
             evaluation.triggered[0].dependency_id=="dependency-good");
      CHECK((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      SemanticDelta new_dependency;new_dependency.delta_id="dependency-delta-2";
      new_dependency.idempotency_key="dependency-key-2";new_dependency.expected_state_version=1;
      new_dependency.nodes={{{"dependency-present",NodeKind::Dependency,"PRESENT",
          {"source-cluster","target-cluster"}}}};
      const auto added=recovered.apply(new_dependency);
      CHECK(added.committed&&added.state_version==2);
      CHECK(recovered.dependency_evaluations().count("dependency-delta-2")==1);
      const auto evaluation2_it=recovered.dependency_evaluations().find("dependency-delta-2"); CHECK(evaluation2_it!=recovered.dependency_evaluations().end()); CHECK(evaluation2_it->second.triggered.size()==2);
      CHECK(std::any_of(evaluation2_it->second.triggered.begin(),
          evaluation2_it->second.triggered.end(),[](const DependencyTrigger& trigger){
            return trigger.dependency_id=="dependency-present";
          }));
    }
    {
      ContextGraph recovered(dependency_path);CHECK(recovered.open());
      CHECK(recovered.state_version()==2);
      CHECK(recovered.dependency_evaluations().count("dependency-delta-1")==1);
      CHECK(recovered.dependency_evaluations().count("dependency-delta-2")==1);
      const auto evaluation_it=recovered.dependency_evaluations().find("dependency-delta-1");
      const auto evaluation2_it=recovered.dependency_evaluations().find("dependency-delta-2");
      CHECK(evaluation_it!=recovered.dependency_evaluations().end());
      CHECK(evaluation2_it!=recovered.dependency_evaluations().end());
      const auto& evaluation=evaluation_it->second;
      CHECK(evaluation.triggered.size()==1 &&
             evaluation.triggered[0].dependency_id=="dependency-good");
      CHECK((evaluation.rejected_dependencies==std::vector<std::string>{"dependency-invalid"}));
      CHECK(std::any_of(evaluation2_it->second.triggered.begin(),
          evaluation2_it->second.triggered.end(),[](const DependencyTrigger& trigger){
            return trigger.dependency_id=="dependency-present";
          }));
    }
  }
  {
    const auto evidence_path=(root/"evidence-temporal-validity.sgj").string();
    {
      ContextGraph graph(evidence_path);CHECK(graph.open());
      SemanticDelta seed;seed.delta_id="evidence-temporal-seed";seed.idempotency_key="evidence-temporal-seed";
      seed.expected_state_version=0;
      seed.nodes={{{"ev-validity-subject",NodeKind::Evidence,"契約書",{}}}};
      seed.evidence_validity_updates.push_back({"ev-validity-subject",
          "2026-01-01T00:00:00+09:00","2026-04-01T00:00:00+09:00",
          "ev-validity-subject",1});
      CHECK(graph.apply(seed).committed && graph.state_version()==1);
      CHECK(graph.evidence_validity_at("ev-validity-subject","2025-12-31T15:00:00Z")==ClaimTemporalState::Valid);
      CHECK(graph.evidence_validity_at("ev-validity-subject","2026-03-31T15:00:00Z")==ClaimTemporalState::Expired);
      CHECK(graph.evidence_validity_at("missing-evidence","2026-02-01T00:00:00Z")==ClaimTemporalState::Unknown);
      SemanticDelta correction;correction.delta_id="evidence-temporal-correction";
      correction.idempotency_key="evidence-temporal-correction";correction.expected_state_version=1;
      correction.evidence_validity_updates.push_back({"ev-validity-subject",
          "2025-12-01T00:00:00Z","2026-05-01T00:00:00Z",
          "ev-validity-subject",2});
      CHECK(graph.apply(correction).committed && graph.state_version()==2);
      CHECK(graph.evidence_validity_history().at("ev-validity-subject").size()==2);
      SemanticDelta invalid;invalid.delta_id="evidence-temporal-invalid";
      invalid.idempotency_key="evidence-temporal-invalid";invalid.expected_state_version=2;
      invalid.evidence_validity_updates.push_back({"ev-validity-subject",
          "2026-05-01T00:00:00Z","2026-04-01T00:00:00Z",
          "ev-validity-subject",3});
      CHECK(graph.apply(invalid).error=="EVIDENCE_VALIDITY_INTERVAL_INVALID");
      CHECK(graph.state_version()==2);
    }
    {
      ContextGraph recovered(evidence_path);CHECK(recovered.open());
      CHECK(recovered.state_version()==2);
      CHECK(recovered.evidence_validity_history().at("ev-validity-subject").size()==2);
      const auto latest=recovered.latest_evidence_validity("ev-validity-subject");
      CHECK(latest && latest->validity.revision==2 && latest->recorded_state_version==2);
      CHECK(recovered.evidence_validity_at("ev-validity-subject","2026-03-01T00:00:00Z")==ClaimTemporalState::Valid);
    }
  }
  // Membership unlink is an append-only journal operation. It removes
  // only the selected Contains edge; shared canonical payload survives.
  const auto unlink_path=(root/"cluster-membership-unlink.sgj").string();
  {
    ContextGraph graph(unlink_path);CHECK(graph.open());
    SemanticDelta seed;seed.delta_id="unlink-seed";seed.idempotency_key="unlink-seed";
    seed.expected_state_version=0;
    seed.nodes={{{"unlink-a",NodeKind::Cluster,"a",{} }},
                {{"unlink-b",NodeKind::Cluster,"b",{} }},
                {{"unlink-shared",NodeKind::Evidence,"shared evidence",{} }}};
    seed.relations={{{"unlink-a-edge",RelationKind::Contains,"unlink-a","unlink-shared"}},
                    {{"unlink-b-edge",RelationKind::Contains,"unlink-b","unlink-shared"}}};
    CHECK(graph.apply(seed).committed);
    CHECK(graph.cluster_membership_reference_count("unlink-shared")==2);
    SemanticDelta unlink;unlink.delta_id="unlink-a-membership";
    unlink.idempotency_key="unlink-a-membership";unlink.expected_state_version=1;
    std::string error;
    CHECK(graph.append_cluster_unmembership(unlink,"unlink-a","unlink-shared",error));
    CHECK(graph.apply(unlink).committed);
    CHECK(graph.relations().count("unlink-a-edge")==0);
    CHECK(graph.relations().count("unlink-b-edge")==1);
    CHECK(graph.nodes().count("unlink-shared")==1);
    CHECK(graph.cluster_membership_reference_count("unlink-shared")==1);
  }
  {
    ContextGraph recovered(unlink_path);CHECK(recovered.open());
    CHECK(recovered.state_version()==2);
    CHECK(recovered.relations().count("unlink-a-edge")==0);
    CHECK(recovered.relations().count("unlink-b-edge")==1);
    CHECK(recovered.nodes().count("unlink-shared")==1);
    CHECK(recovered.cluster_membership_reference_count("unlink-shared")==1);
    std::string error;
    SemanticDelta last;last.delta_id="unlink-last";last.idempotency_key="unlink-last";
    last.expected_state_version=2;
    CHECK(recovered.append_cluster_unmembership(last,"unlink-b","unlink-shared",error));
    CHECK(recovered.apply(last).committed);
    CHECK(recovered.nodes().count("unlink-shared")==1);
    CHECK(recovered.relations().count("unlink-b-edge")==0);
    CHECK(recovered.cluster_membership_reference_count("unlink-shared")==0);
  }
  {
    ContextGraph recovered(unlink_path);CHECK(recovered.open());
    CHECK(recovered.nodes().count("unlink-shared")==1);
    CHECK(recovered.relations().empty());
  }
  // Entity reclamation is permitted only after all cluster memberships and
  // every graph-owned reference are removed in the same durable transaction.
  const auto entity_reclaim_path=(root/"entity-reclamation.sgj").string();
  {
    ContextGraph graph(entity_reclaim_path);CHECK(graph.open());
    SemanticDelta seed;seed.delta_id="entity-reclaim-seed";
    seed.idempotency_key="entity-reclaim-seed";seed.expected_state_version=0;
    seed.nodes={{{"reclaim-cluster",NodeKind::Cluster,"cluster",{} }},
                {{"reclaim-entity",NodeKind::Entity,"orphan entity",{} }}};
    seed.relations={{{"reclaim-membership",RelationKind::Contains,
        "reclaim-cluster","reclaim-entity"}}};
    CHECK(graph.apply(seed).committed);
    SemanticDelta reclaim;reclaim.delta_id="entity-reclaim-commit";
    reclaim.idempotency_key="entity-reclaim-commit";reclaim.expected_state_version=1;
    std::string error;
    CHECK(graph.append_cluster_unmembership(reclaim,"reclaim-cluster",
        "reclaim-entity",error));
    CHECK(graph.append_entity_reclamation(reclaim,"reclaim-entity",error));
    const auto committed=graph.apply(reclaim);
    CHECK(committed.committed&&committed.state_version==2);
    CHECK(graph.nodes().count("reclaim-entity")==0);
    CHECK(graph.nodes().count("reclaim-cluster")==1);
  }
  {
    ContextGraph recovered(entity_reclaim_path);CHECK(recovered.open());
    CHECK(recovered.state_version()==2);
    CHECK(recovered.nodes().count("reclaim-entity")==0);
  }
  // A snapshot taken before reclamation plus the committed journal tail
  // must restore to the same graph as full replay.
  const auto entity_snapshot_path=(root/"entity-reclamation-snapshot.sgj").string();
  const auto entity_snapshot_target=(root/"entity-reclamation-snapshot-target.sgj").string();
  {
    EventJournal journal(entity_snapshot_path);CHECK(journal.open());
    ContextGraph graph(journal);CHECK(graph.open());
    SemanticDelta seed;seed.delta_id="entity-snapshot-seed";
    seed.idempotency_key="entity-snapshot-seed";seed.expected_state_version=0;
    seed.nodes={{{"snapshot-cluster",NodeKind::Cluster,"cluster",{} }},
                {{"snapshot-entity",NodeKind::Entity,"entity",{} }}};
    seed.relations={{{"snapshot-membership",RelationKind::Contains,
        "snapshot-cluster","snapshot-entity"}}};
    CHECK(graph.apply(seed).committed);
    ContextGraphSnapshotDomain domain(graph);std::string snapshot,error;
    CHECK(domain.capture(&snapshot,&error));
    SemanticDelta reclaim;reclaim.delta_id="entity-snapshot-reclaim";
    reclaim.idempotency_key="entity-snapshot-reclaim";reclaim.expected_state_version=1;
    CHECK(graph.append_cluster_unmembership(reclaim,"snapshot-cluster",
        "snapshot-entity",error));
    CHECK(graph.append_entity_reclamation(reclaim,"snapshot-entity",error));
    CHECK(graph.apply(reclaim).committed);
    const auto tail=journal.committed_tail(1);
    CHECK(tail.ok&&tail.current_state_version==2);
    EventJournal target_journal(entity_snapshot_target);CHECK(target_journal.open());
    ContextGraph target(target_journal);CHECK(target.open());
    ContextGraphSnapshotDomain target_domain(target);
    auto prepared=target_domain.prepare_restore_with_tail(snapshot,tail.events,&error);
    CHECK(prepared!=nullptr);
    prepared->commit();
    CHECK(target.nodes().count("snapshot-entity")==0);
    CHECK(target.nodes().count("snapshot-cluster")==1);
  }
  // Reclamation fails closed while any non-membership relation still points
  // to the entity, even when its cluster membership has already been removed.
  const auto entity_guard_path=(root/"entity-reclamation-guard.sgj").string();
  {
    ContextGraph graph(entity_guard_path);CHECK(graph.open());
    SemanticDelta seed;seed.delta_id="entity-guard-seed";
    seed.idempotency_key="entity-guard-seed";seed.expected_state_version=0;
    seed.nodes={{{"guard-entity",NodeKind::Entity,"referenced entity",{} }},
                {{"guard-question",NodeKind::Question,"question",{} }}};
    seed.relations={{{"guard-about",RelationKind::About,"guard-question","guard-entity"}}};
    CHECK(graph.apply(seed).committed);
    SemanticDelta reclaim;reclaim.delta_id="entity-guard-reclaim";
    reclaim.idempotency_key="entity-guard-reclaim";reclaim.expected_state_version=1;
    std::string error;
    CHECK(!graph.append_entity_reclamation(reclaim,"guard-entity",error));
    CHECK(error=="ENTITY_RECLAMATION_RELATION_REFERENCES_REMAIN");
    CHECK(graph.nodes().count("guard-entity")==1);
  }
  std::filesystem::remove_all(root);const auto path=(root/"context.sgj").string();
  {
    ContextGraph graph(path);CHECK(graph.open());
    SemanticDelta d;d.delta_id="d1";d.idempotency_key="k1";d.expected_state_version=0;
    d.nodes={{{"cluster-1",NodeKind::Cluster,"予算案",{} }},
             {{"evidence-1",NodeKind::Evidence,"会議発言",{} }},
             {{"claim-1",NodeKind::Claim,"予算を増額する",{"evidence-1"}}},
             {{"question-1",NodeKind::Question,"予算は増額可能か",{} }},
             {{"aspect-1",NodeKind::QuestionAspect,"財源の有無",{"question-1"}}},
             {{"interest-1",NodeKind::Interest,"財源を確認",{"aspect-1","cluster-1"}}}};
    d.relations={{{"rel-1",RelationKind::About,"claim-1","cluster-1"}}};
    d.focus=SetFocus{{"cluster-1","interest-1"}};
    auto result=graph.apply(d);CHECK(result.committed && result.state_version==1);
    CHECK(graph.nodes().size()==6 && graph.relations().size()==1);
    CHECK(graph.focus() && graph.focus()->interest_id=="interest-1");
    auto retry=graph.apply(d);CHECK(retry.committed && retry.duplicate && graph.state_version()==1);
    auto conflict=d;conflict.nodes[0].node.content="別の内容";
    CHECK(graph.apply(conflict).error=="IDEMPOTENCY_KEY_REUSED");
    SemanticDelta invalid;invalid.delta_id="d2";invalid.idempotency_key="k2";invalid.expected_state_version=1;
    invalid.relations={{{"bad-rel",RelationKind::Contradicts,"claim-1","cluster-1"}}};
    CHECK(graph.apply(invalid).error=="CLAIM_RELATION_TYPE_INVALID");
    CHECK(graph.state_version()==1);
    SemanticDelta stale;stale.delta_id="d3";stale.idempotency_key="k3";stale.expected_state_version=0;
    stale.nodes={{{"cluster-2",NodeKind::Cluster,"別議題",{}}}};
    CHECK(graph.apply(stale).error=="STATE_VERSION_CONFLICT");
  }
  {
    ContextGraph recovered(path);CHECK(recovered.open());
    CHECK(recovered.state_version()==1);
    CHECK(recovered.nodes().size()==6 && recovered.relations().size()==1);
    CHECK(recovered.focus() && recovered.focus()->cluster_id=="cluster-1");
    CHECK(recovered.focus_history().size()==1);
    SemanticDelta original;original.delta_id="d1";original.idempotency_key="k1";original.expected_state_version=0;
    original.nodes={{{"cluster-1",NodeKind::Cluster,"予算案",{}}},{{"evidence-1",NodeKind::Evidence,"会議発言",{}}},{{"claim-1",NodeKind::Claim,"予算を増額する",{"evidence-1"}}},{{"question-1",NodeKind::Question,"予算は増額可能か",{}}},{{"aspect-1",NodeKind::QuestionAspect,"財源の有無",{"question-1"}}},{{"interest-1",NodeKind::Interest,"財源を確認",{"aspect-1","cluster-1"}}}};
    original.relations={{{"rel-1",RelationKind::About,"claim-1","cluster-1"}}};
    original.focus=SetFocus{{"cluster-1","interest-1"}};
    auto replay_retry=recovered.apply(original);CHECK(replay_retry.committed && replay_retry.duplicate);
    SemanticDelta d;d.delta_id="d2";d.idempotency_key="k2";d.expected_state_version=1;
    d.nodes={{{"cluster-2",NodeKind::Cluster,"派生議題",{}}}};
    auto result=recovered.apply(d);CHECK(result.committed && result.state_version==2);
  }
  {
    ContextGraph recovered(path);CHECK(recovered.open());
    CHECK(recovered.state_version()==2);
    CHECK(recovered.nodes().count("cluster-2")==1);
    CHECK(recovered.nodes().count("cluster-1")==1);
  }
  {
    const auto chaos_path=(root/"chaos-focus.sgj").string();
    {
      ContextGraph graph(chaos_path);CHECK(graph.open());
      SemanticDelta seed;seed.delta_id="chaos-focus-seed";seed.idempotency_key="chaos-focus-seed";
      seed.expected_state_version=0;
      seed.nodes={{{"chaos-1",NodeKind::ChaosCluster,"Chaos Cluster chaos-1",{}}}};
      seed.focus=SetFocus{{"chaos-1","",true}};
      auto result=graph.apply(seed);CHECK(result.committed);
      CHECK(graph.focus()&&graph.focus()->chaos_cluster);
    }
    ContextGraph recovered(chaos_path);CHECK(recovered.open());
    CHECK(recovered.focus()&&recovered.focus()->cluster_id=="chaos-1"&&recovered.focus()->chaos_cluster);
  }
  {
    const auto claim_path=(root/"claim-lifecycle.sgj").string();
    {
      ContextGraph graph(claim_path);CHECK(graph.open());
      SemanticDelta seed;seed.delta_id="claim-seed";seed.idempotency_key="claim-seed";
      seed.expected_state_version=0;
      seed.nodes={{{"evidence-old",NodeKind::Evidence,"当初の会議記録",{}}},
                  {{"claim-old",NodeKind::Claim,"予算は100万円",{"evidence-old"}}}};
      CHECK(graph.apply(seed).committed);
      CHECK(graph.claim_status("claim-old")==ClaimStatus::Active);
      SemanticDelta correction;correction.delta_id="claim-correction";
      correction.idempotency_key="claim-correction";correction.expected_state_version=1;
      correction.nodes={{{"evidence-new",NodeKind::Evidence,"訂正後の会議記録",{}}},
                        {{"claim-new",NodeKind::Claim,"予算は120万円",{"evidence-new"}}}};
      correction.relations={{{"claim-supersedes",RelationKind::Supersedes,
                              "claim-new","claim-old"}}};
      CHECK(graph.apply(correction).committed);
      CHECK(graph.claim_status("claim-old")==ClaimStatus::Superseded);
      CHECK(graph.claim_status("claim-new")==ClaimStatus::Active);
      SemanticDelta invalid;invalid.delta_id="claim-double-supersede";
      invalid.idempotency_key="claim-double-supersede";invalid.expected_state_version=2;
      invalid.relations={{{"claim-supersedes-again",RelationKind::Supersedes,
                           "claim-new","claim-old"}}};
      CHECK(graph.apply(invalid).error=="CLAIM_NOT_ACTIVE_FOR_SUPERSESSION");
      CHECK(graph.state_version()==2);
      SemanticDelta retract;retract.delta_id="claim-retract";
      retract.idempotency_key="claim-retract";retract.expected_state_version=2;
      retract.nodes={{{"evidence-retract",NodeKind::Evidence,"根拠の誤りを確認",{}}}};
      retract.relations={{{"claim-retracted",RelationKind::Retracts,
                           "evidence-retract","claim-new"}}};
      CHECK(graph.apply(retract).committed);
      CHECK(graph.claim_status("claim-new")==ClaimStatus::Retracted);
      CHECK(graph.claim_status("claim-old")==ClaimStatus::Superseded);
    }
    {
      ContextGraph recovered(claim_path);CHECK(recovered.open());
      CHECK(recovered.state_version()==3);
      CHECK(recovered.claim_status("claim-old")==ClaimStatus::Superseded);
      CHECK(recovered.claim_status("claim-new")==ClaimStatus::Retracted);
      CHECK(recovered.relations().count("claim-supersedes")==1);
      CHECK(recovered.relations().count("claim-retracted")==1);
    }
  }
  {
    const auto temporal_path=(root/"claim-temporal-validity.sgj").string();
    {
      ContextGraph graph(temporal_path);CHECK(graph.open());
      SemanticDelta seed;seed.delta_id="temporal-seed";seed.idempotency_key="temporal-seed";
      seed.expected_state_version=0;
      seed.nodes={{{"temporal-evidence-1",NodeKind::Evidence,"契約書",{}}},
                  {{"temporal-claim",NodeKind::Claim,"契約は2026年1月から有効",{"temporal-evidence-1"}}}};
      seed.claim_validity_updates.push_back({"temporal-claim",
          "2026-01-01T00:00:00+09:00","2026-04-01T00:00:00+09:00",
          "temporal-evidence-1",1});
      CHECK(graph.apply(seed).committed && graph.state_version()==1);
      CHECK(graph.claim_validity_at("temporal-claim","2025-12-31T14:59:59Z")==ClaimTemporalState::NotYetValid);
      CHECK(graph.claim_validity_at("temporal-claim","2025-12-31T15:00:00Z")==ClaimTemporalState::Valid);
      CHECK(graph.claim_validity_at("temporal-claim","2026-03-31T15:00:00Z")==ClaimTemporalState::Expired);
      CHECK(graph.claim_validity_at("missing-claim","2026-02-01T00:00:00Z")==ClaimTemporalState::Unknown);
      SemanticDelta correction;correction.delta_id="temporal-correction";
      correction.idempotency_key="temporal-correction";correction.expected_state_version=1;
      correction.nodes={{{"temporal-evidence-2",NodeKind::Evidence,"契約開始日の訂正",{}}}};
      correction.relations={{{"temporal-evidence-2-supports-claim",RelationKind::Supports,
          "temporal-evidence-2","temporal-claim"}}};
      correction.claim_validity_updates.push_back({"temporal-claim",
          "2025-12-01T00:00:00Z","2026-04-01T00:00:00Z",
          "temporal-evidence-2",2});
      CHECK(graph.apply(correction).committed && graph.state_version()==2);
      CHECK(graph.claim_validity_at("temporal-claim","2025-12-15T00:00:00Z")==ClaimTemporalState::Valid);
      CHECK(graph.claim_validity_history().at("temporal-claim").size()==2);
      SemanticDelta invalid;invalid.delta_id="temporal-invalid";
      invalid.idempotency_key="temporal-invalid";invalid.expected_state_version=2;
      invalid.claim_validity_updates.push_back({"temporal-claim",
          "2026-05-01T00:00:00Z","2026-04-01T00:00:00Z",
          "temporal-evidence-2",3});
      CHECK(graph.apply(invalid).error=="CLAIM_VALIDITY_INTERVAL_INVALID");
      CHECK(graph.state_version()==2);
    }
    {
      ContextGraph recovered(temporal_path);CHECK(recovered.open());
      CHECK(recovered.state_version()==2);
      CHECK(recovered.claim_validity_history().at("temporal-claim").size()==2);
      const auto latest=recovered.latest_claim_validity("temporal-claim");
      CHECK(latest && latest->validity.revision==2);
      CHECK(latest->recorded_state_version==2);
      CHECK(recovered.claim_validity_at("temporal-claim","2025-12-15T00:00:00Z")==ClaimTemporalState::Valid);
    }
  }
  {
    std::filesystem::create_directories(root);
    const auto propagation_path=(root/"temporal-dependency-propagation.sgj").string();
    {
      ContextGraph graph(propagation_path);if(!graph.open())throw std::runtime_error("temporal propagation journal open failed");
      SemanticDelta seed;seed.delta_id="temporal-propagation-seed";
      seed.idempotency_key="temporal-propagation-seed";seed.expected_state_version=0;
      seed.nodes={
        {{"basis-1",NodeKind::Evidence,"契約書",{}}},
        {{"claim-period",NodeKind::Claim,"契約期間",{ "basis-1" }}},
        {{"valid-action",NodeKind::Cluster,"有効期間内の処理",{}}},
        {{"expired-action",NodeKind::Cluster,"失効後の処理",{}}},
        {{"dep-valid-period",NodeKind::Dependency,
          "CLAIM_TEMPORAL_STATE:VALID@2026-02-01T00:00:00Z",
          {"claim-period","valid-action"}}},
        {{"dep-expired-period",NodeKind::Dependency,
          "CLAIM_TEMPORAL_STATE:EXPIRED@2026-02-01T00:00:00Z",
          {"claim-period","expired-action"}}}
      };
      seed.claim_validity_updates.push_back({"claim-period","2026-01-01T00:00:00Z",
          "2026-04-01T00:00:00Z","basis-1",1});
      const auto seed_result=graph.apply(seed);
      if(!seed_result.committed)throw std::runtime_error("temporal seed rejected: "+seed_result.error);
      const auto first_it=graph.dependency_evaluations().find("temporal-propagation-seed");
      if(first_it==graph.dependency_evaluations().end())throw std::runtime_error("temporal seed evaluation missing");
      const auto& first=first_it->second;
      CHECK(first.triggered.size()==1 && first.triggered[0].dependency_id=="dep-valid-period");
      SemanticDelta correction;correction.delta_id="temporal-propagation-correction";
      correction.idempotency_key="temporal-propagation-correction";
      correction.expected_state_version=1;
      correction.nodes={{{"basis-2",NodeKind::Evidence,"契約期間の訂正",{}}}};
      correction.relations={{{"basis-2-supports-claim",RelationKind::Supports,
          "basis-2","claim-period"}}};
      correction.claim_validity_updates.push_back({"claim-period","2025-12-01T00:00:00Z",
          "2026-01-15T00:00:00Z","basis-2",2});
      const auto correction_result=graph.apply(correction);
      if(!correction_result.committed)throw std::runtime_error("temporal correction rejected: "+correction_result.error);
      const auto second_it=graph.dependency_evaluations().find("temporal-propagation-correction");
      if(second_it==graph.dependency_evaluations().end())throw std::runtime_error("temporal correction evaluation missing");
      const auto& second=second_it->second;
      CHECK(second.triggered.size()==1 && second.triggered[0].dependency_id=="dep-expired-period");
      CHECK(std::find(second.impacted_nodes.begin(),second.impacted_nodes.end(),
          "expired-action")!=second.impacted_nodes.end());
    }
    {
      ContextGraph recovered(propagation_path);if(!recovered.open())throw std::runtime_error("temporal propagation replay open failed");
      CHECK(recovered.state_version()==2);
      const auto second_it=recovered.dependency_evaluations().find("temporal-propagation-correction");
      if(second_it==recovered.dependency_evaluations().end())throw std::runtime_error("replayed temporal evaluation missing");
      const auto& second=second_it->second;
      CHECK(second.triggered.size()==1 && second.triggered[0].dependency_id=="dep-expired-period");
      CHECK(recovered.claim_validity_history().at("claim-period").size()==2);
    }
  }

  {
    const auto reassessment_path=(root/"temporal-reassessment-core.sgj").string();
    {
      ContextGraph graph(reassessment_path);CHECK(graph.open());
      SemanticDelta seed;seed.delta_id="reassessment-seed";seed.idempotency_key="reassessment-seed";
      seed.expected_state_version=0;
      seed.nodes={{{"reassess-support",NodeKind::Evidence,"現行の契約台帳",{}}},
                  {{"reassess-contradiction",NodeKind::Evidence,"旧契約の記録",{}}},
                  {{"reassess-claim",NodeKind::Claim,"契約は有効",{ "reassess-support" }}}};
      seed.relations={{{"reassess-support-rel",RelationKind::Supports,"reassess-support","reassess-claim"}},
                      {{"reassess-contradiction-rel",RelationKind::Contradicts,"reassess-contradiction","reassess-claim"}}};
      seed.claim_validity_updates.push_back({"reassess-claim","2026-01-01T00:00:00Z","2027-01-01T00:00:00Z","reassess-support",1});
      seed.evidence_validity_updates.push_back({"reassess-support","2026-01-01T00:00:00Z","2027-01-01T00:00:00Z","reassess-support",1});
      seed.evidence_validity_updates.push_back({"reassess-contradiction","2025-01-01T00:00:00Z","2025-12-31T00:00:00Z","reassess-contradiction",1});
      const auto applied=graph.apply(seed);CHECK(applied.committed&&applied.state_version==1);
      TemporalReassessmentRecord trace;std::string error;
      CHECK(graph.reassess_claim("reassess-claim","2026-06-01T00:00:00Z",1,trace,error));
      CHECK(trace.decision=="SUPPORTED"&&trace.evaluated_state_version==1&&trace.recorded_state_version==2);
      CHECK(trace.claim_validity_revision==1&&trace.claim_validity_basis_evidence_id=="reassess-support"&&trace.evidence.size()==2);
      CHECK(trace.active_support_ids.size()==1&&trace.active_contradiction_ids.empty());
      CHECK(trace.evidence[0].validity_revision==1&&trace.evidence[0].basis_evidence_id==trace.evidence[0].evidence_id);
      TemporalReassessmentRecord stale;
      CHECK(!graph.reassess_claim("reassess-claim","2026-06-01T00:00:00Z",1,stale,error));
      CHECK(error=="STATE_VERSION_CONFLICT");
      SemanticDelta add_conflict;add_conflict.delta_id="reassessment-add-conflict";
      add_conflict.idempotency_key="reassessment-add-conflict";add_conflict.expected_state_version=2;
      add_conflict.evidence_validity_updates.push_back({"reassess-contradiction","2026-01-01T00:00:00Z","2027-01-01T00:00:00Z","reassess-contradiction",2});
      CHECK(graph.apply(add_conflict).committed&&graph.state_version()==3);
      TemporalReassessmentRecord conflict;
      CHECK(graph.reassess_claim("reassess-claim","2026-06-01T00:00:00Z",3,conflict,error));
      CHECK(conflict.decision=="TEMPORAL_CONFLICT"&&conflict.active_contradiction_ids.size()==1);
      CHECK(conflict.sequence==2&&conflict.previous_hash==trace.chain_hash);
    }
    {
      ContextGraph recovered(reassessment_path);CHECK(recovered.open());
      CHECK(recovered.state_version()==4);
      const auto history_it=recovered.reassessment_history().find("reassess-claim");
      CHECK(history_it!=recovered.reassessment_history().end());
      const auto& history=history_it->second;
      CHECK(history.size()==2);
      CHECK(history[0].decision=="SUPPORTED"&&history[1].decision=="TEMPORAL_CONFLICT");
      CHECK(history[1].previous_hash==history[0].chain_hash);
      CHECK(history[1].evaluated_state_version==3&&history[1].recorded_state_version==4);
    }
  }
  // Shared-journal owner event: ContextGraph must advance the global
  // transaction version without interpreting retention metadata as graph data.
  const auto retention_path=(root/"retention-owner-events.sgj").string();
  {
    EventJournal journal(retention_path);
    ContextGraph graph(journal); CHECK(graph.open());
    const auto committed=journal.commit("retention-manifest-tx",
      "retention-manifest-idem",0,
      {{"retention:manifest:test","RETENTION_MANIFEST_RECORDED","{}"}});
    CHECK(committed.committed&&committed.state_version==1);
    const auto tombstone=journal.commit("retention-tombstone-tx",
      "retention-tombstone-idem",1,
      {{"retention:tombstone:test","RETENTION_TOMBSTONE_PROPOSED","{}"}});
    CHECK(tombstone.committed&&tombstone.state_version==2);
    const auto provenance=journal.commit("evidence-provenance-tx",
      "evidence-provenance-idem",2,
      {{"evidence-provenance:test:1","EVIDENCE_PROVENANCE_SET_V1","{}"}});
    CHECK(provenance.committed&&provenance.state_version==3);
  }
  {
    ContextGraph recovered(retention_path);
    CHECK(recovered.open());
    CHECK(recovered.state_version()==3);
    CHECK(recovered.nodes().empty());
  }
  // Input information is assigned to deterministic typed clusters by Core,
  // and the parent/member containment survives journal replay.
  const auto cluster_path=(root/"classified-input-clusters.sgj").string();
  std::string classified_cluster_id;
  {
    ContextGraph graph(cluster_path); CHECK(graph.open());
    SemanticDelta delta;
    delta.delta_id="cluster-classification-delta";
    delta.idempotency_key="cluster-classification-idem";
    delta.expected_state_version=0;
    delta.nodes={{{"input-root",NodeKind::Cluster,"stream context",{}}},
                 {{"evidence-a",NodeKind::Evidence,"classification evidence",{}}},
                 {{"claim-a",NodeKind::Claim,"candidate claim",{"evidence-a"}}}};
    std::string error;
    CHECK(graph.append_cluster_membership(delta,"input-root","LEXICAL_INTERPRETATION",
        "source-digest:span-0-3","claim-a","classification-test",
        classified_cluster_id,error));
    const auto stable_cluster_id=classified_cluster_id;
    CHECK(graph.append_cluster_membership(delta,"input-root","LEXICAL_INTERPRETATION",
        "source-digest:span-0-3","claim-a","classification-test",
        classified_cluster_id,error));
    CHECK(classified_cluster_id==stable_cluster_id);
    std::string second_cluster_id;
    CHECK(graph.append_cluster_membership(delta,"input-root","SURFACE_REALIZATION",
        "source-digest:span-0-3","claim-a","classification-test-surface",
        second_cluster_id,error));
    CHECK(second_cluster_id!=classified_cluster_id);
    std::string invalid_cluster_id;
    CHECK(!graph.append_cluster_membership(delta,"missing-parent","TOPIC",
        "partition","claim-a","invalid-parent",invalid_cluster_id,error));
    CHECK(error=="CLUSTER_CLASSIFICATION_PARENT_NOT_FOUND");
    CHECK(graph.append_cluster_membership(delta,"input-root","A|B","C",
        "claim-a","invalid-parent",invalid_cluster_id,error));
    const auto delimiter_a=invalid_cluster_id;
    CHECK(graph.append_cluster_membership(delta,"input-root","A","B|C",
        "claim-a","delimiter-test",invalid_cluster_id,error));
    CHECK(invalid_cluster_id!=delimiter_a);
    CHECK(!graph.append_cluster_membership(delta,"input-root","TOPIC",
        "partition","missing-member","invalid-member",invalid_cluster_id,error));
    CHECK(error=="CLUSTER_CLASSIFICATION_MEMBER_NOT_FOUND");
    CHECK(!graph.append_cluster_membership(delta,"input-root","TOPIC",
        "partition","claim-a",std::string(257,'x'),invalid_cluster_id,error));
    CHECK(error=="CLUSTER_CLASSIFICATION_INPUT_INVALID");
    SemanticDelta conflict_delta;
    conflict_delta.nodes={{{"input-root",NodeKind::Cluster,"stream context",{}}},
                          {{"claim-a",NodeKind::Claim,"candidate claim",{}}}};
    conflict_delta.relations.push_back({{delta.relations.front().relation.id,
        RelationKind::Contains,"wrong-parent","wrong-child"}});
    const auto node_count_before=conflict_delta.nodes.size();
    const auto relation_count_before=conflict_delta.relations.size();
    CHECK(!graph.append_cluster_membership(conflict_delta,"input-root",
        "LEXICAL_INTERPRETATION","source-digest:span-0-3","claim-a",
        "classification-test",invalid_cluster_id,error));
    CHECK(error=="CLUSTER_CLASSIFICATION_RELATION_ID_CONFLICT");
    CHECK(conflict_delta.nodes.size()==node_count_before);
    CHECK(conflict_delta.relations.size()==relation_count_before);
    CHECK(!classified_cluster_id.empty());
    const auto classified_commit = graph.apply(delta);
    if (!classified_commit.committed) std::cerr << "cluster classification commit failed: " << classified_commit.error << '\n';
    CHECK(classified_commit.committed);
    CHECK(graph.nodes().at(classified_cluster_id).kind==NodeKind::Cluster);
    CHECK(std::any_of(graph.relations().begin(),graph.relations().end(),
        [&](const auto& item){return item.second.from=="input-root" &&
          item.second.to==classified_cluster_id && item.second.kind==RelationKind::Contains;}));
    CHECK(std::any_of(graph.relations().begin(),graph.relations().end(),
        [&](const auto& item){return item.second.from==classified_cluster_id &&
          item.second.to=="claim-a" && item.second.kind==RelationKind::Contains;}));
  }
  {
    ContextGraph recovered(cluster_path); CHECK(recovered.open());
    CHECK(recovered.nodes().count(classified_cluster_id)==1);
    CHECK(std::any_of(recovered.relations().begin(),recovered.relations().end(),
        [&](const auto& item){return item.second.from==classified_cluster_id &&
          item.second.to=="claim-a";}));
  }

  std::filesystem::remove_all(root);
  std::cout<<"SURGE ContextGraph tests: retention event ownership passed\n";
}
