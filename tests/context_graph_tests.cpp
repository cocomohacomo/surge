#include "test_support.hpp"
#include "surge/context_graph.hpp"
#include "surge/focus_evaluation_snapshot.hpp"
#include "surge/snapshot_domains.hpp"
#include "surge/snapshot_store.hpp"
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
    if(!prepared)std::cerr<<"entity reclamation snapshot-tail restore failed: "<<error<<"\n";
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
  // A composed recognition is persisted as a new cluster and can become the focused observation.
  {
    const auto path=(root/"derived-cognition-cluster.sgj").string();
    ContextGraph graph(path);CHECK(graph.open());
    SemanticDelta seed;seed.delta_id="derived-seed";seed.idempotency_key="derived-seed";
    seed.expected_state_version=0;
    seed.nodes={{{"source-cluster-a",NodeKind::Cluster,"experience A",{}}},
                {{"source-cluster-b",NodeKind::Cluster,"experience B",{}}}};
    CHECK(graph.apply(seed).committed);
    SemanticDelta composed;composed.delta_id="derived-compose";
    composed.idempotency_key="derived-compose";composed.expected_state_version=graph.state_version();
    std::string error;
    CHECK(graph.append_derived_cognition_cluster(composed,"new-cognition",
        "A+B newly recognized state",{"source-cluster-a","source-cluster-b"},
        "derived-lineage",error));
    composed.focus=SetFocus{{"new-cognition","",false}};
    CHECK(graph.apply(composed).committed);
    const auto focused=graph.observe_focused_cognition();
    CHECK(focused.current_focus && focused.current_focus->cluster_id=="new-cognition");
    CHECK(focused.nodes.size()==1 && focused.nodes.front().id=="new-cognition");
    const auto derived_relations=graph.relations();
    CHECK(std::count_if(derived_relations.begin(),derived_relations.end(),
        [](const auto& item){return item.second.kind==RelationKind::DerivedFrom &&
          item.second.from=="new-cognition";})==2);
    CHECK(graph.observe_cognition().nodes.size()==3);
  }

  // Spatial focus selects a 3D region; per-cluster depths remain independent.
  {
    const auto path=(root/"spatial-focus.sgj").string();
    std::string spatial_error;
    {
    ContextGraph graph(path);CHECK(graph.open());
    SemanticDelta seed;seed.delta_id="spatial-focus-seed";
    seed.idempotency_key="spatial-focus-seed";seed.expected_state_version=0;
    seed.nodes={{{"spatial-a",NodeKind::Cluster,"A",{} }},
                {{"spatial-b",NodeKind::Cluster,"B",{} }},
                {{"spatial-c",NodeKind::Cluster,"C",{} }},
                {{"spatial-nested",NodeKind::Cluster,"nested",{} }},
                {{"spatial-member",NodeKind::Entity,"member",{} }},
                {{"spatial-deep",NodeKind::Entity,"deep member",{} }}};
    seed.relations={{{"spatial-a-nested",RelationKind::Contains,"spatial-a","spatial-nested"}},
                    {{"spatial-a-member",RelationKind::Contains,"spatial-a","spatial-member"}},
                    {{"spatial-nested-deep",RelationKind::Contains,"spatial-nested","spatial-deep"}}};
    CHECK(graph.apply(seed).committed);
    const FocusRegion compact{{0.0,0.0,0.0},{1.0,1.0,1.0}};
    const std::vector<ClusterSpatialPosition> positions={
      {"spatial-a",{-0.5,0.0,0.0}},{"spatial-b",{0.5,0.0,0.0}},
      {"spatial-c",{5.0,0.0,0.0}}};
    const std::vector<ClusterFocusDepth> depths={
      {"spatial-a",1},{"spatial-b",0},{"spatial-nested",0}};
    const auto focused=graph.observe_focused_cognition(compact,positions,depths,20,20);
    CHECK(focused.focus_region.has_value());
    CHECK(focused.focus_work.clusters_considered==3);
    CHECK(focused.focus_work.clusters_in_region==2);
    CHECK(focused.nodes.size()==4);
    CHECK(focused.focus_work.nodes_processed==4);
    CHECK(focused.focus_work.relations_processed==2);
    CHECK(focused.focus_work.relations_examined==3);
    CHECK(focused.focus_work.relations_traversed==2);
    CHECK(focused.focus_work.work_units==12);
    CHECK(!focused.focus_work.budget_exhausted);
    const FocusRegion expanded{{0.0,0.0,0.0},{10.0,1.0,1.0}};
    const auto wider=graph.observe_focused_cognition(expanded,positions,depths,20,20);
    CHECK(wider.focus_work.clusters_in_region==3);
    CHECK(wider.nodes.size()>focused.nodes.size());
    const std::vector<ClusterFocusDepth> deeper={
      {"spatial-a",1},{"spatial-b",0},{"spatial-nested",1}};
    const auto deeper_view=graph.observe_focused_cognition(compact,positions,deeper,20,20);
    CHECK(deeper_view.nodes.size()==focused.nodes.size()+1);
    const std::vector<ClusterFocusDepth> radius_two={
      {"spatial-a",2},{"spatial-b",0},{"spatial-nested",0}};
    const auto two_levels=graph.observe_focused_cognition(compact,positions,radius_two,20,20);
    CHECK(two_levels.nodes.size()==focused.nodes.size()+1);
    const auto bounded=graph.observe_focused_cognition(expanded,positions,deeper,2,20);
    CHECK(bounded.focus_work.budget_exhausted);
    CHECK(bounded.nodes.size()<=2);
    CHECK(bounded.logical_region_cluster_ids.size()==3);
    CHECK(bounded.processed_region_cluster_ids.empty());
    CHECK(bounded.deferred_region_cluster_ids.size()==3);
    CHECK(bounded.focus_work.clusters_in_region==3);
    const auto relation_bounded=graph.observe_focused_cognition(expanded,positions,deeper,20,1);
    CHECK(relation_bounded.focus_work.budget_exhausted);
    CHECK(relation_bounded.focus_work.relations_examined==1);
    CHECK(relation_bounded.logical_region_cluster_ids.size()==3);
    CHECK(relation_bounded.processed_region_cluster_ids.empty());
    CHECK(relation_bounded.deferred_region_cluster_ids.size()==3);

    // Unrelated outgoing Relations must not consume this Focus projection's budget.
    SemanticDelta unrelated_edges;
    unrelated_edges.delta_id="spatial-unrelated-edges";
    unrelated_edges.idempotency_key="spatial-unrelated-edges";
    unrelated_edges.expected_state_version=graph.state_version();
    for (std::size_t i=0;i<32;++i) {
      GraphRelation edge;
      edge.id="spatial-unrelated-edge-"+std::to_string(i);
      edge.kind=RelationKind::DerivedFrom;
      edge.from="spatial-c";
      edge.to="spatial-a";
      unrelated_edges.relations.push_back({edge});
    }
    CHECK(graph.apply(unrelated_edges).committed);
    const auto local_budget=graph.observe_focused_cognition(
        compact,positions,deeper,20,20);
    CHECK(local_budget.logical_region_cluster_ids.size()==2);
    CHECK(!local_budget.focus_work.budget_exhausted);
    CHECK(local_budget.focus_work.relations_examined<10);

    // The same nested cluster is reachable from two roots with different
    // depth limits. Expansion must be tracked per origin root, not globally.
    SemanticDelta shared_path;
    shared_path.delta_id="spatial-shared-root-path";
    shared_path.idempotency_key="spatial-shared-root-path";
    shared_path.expected_state_version=graph.state_version();
    shared_path.relations={{{"spatial-b-shared-nested",RelationKind::Contains,
        "spatial-b","spatial-nested"}}};
    CHECK(graph.apply(shared_path).committed);
    const std::vector<ClusterFocusDepth> shared_depths={
      {"spatial-a",1},{"spatial-b",2},{"spatial-nested",2}};
    const auto shared_unbounded=graph.observe_focused_cognition(
        compact,positions,shared_depths,1000,1000);
    CHECK(std::any_of(shared_unbounded.nodes.begin(),shared_unbounded.nodes.end(),
        [](const auto& node){return node.id=="spatial-deep";}));

    // Resumable traversal must converge to the same graph projection as one
    // unbounded pass. Tiny independent budgets force suspension inside a root.
    const auto unbounded_focus = shared_unbounded;
    FocusTraversalCursor node_limited_cursor;
    const auto node_limited = graph.observe_focused_cognition(
        compact,positions,shared_depths,node_limited_cursor,1,100);
    CHECK(node_limited.focus_work.nodes_processed<=1);
    CHECK(node_limited.focus_work.relations_examined<=100);
    FocusTraversalCursor relation_limited_cursor;
    const auto relation_limited = graph.observe_focused_cognition(
        compact,positions,shared_depths,relation_limited_cursor,100,1);
    CHECK(relation_limited.focus_work.relations_examined<=1);
    CHECK(relation_limited.focus_work.nodes_processed>1);
    FocusTraversalCursor traversal_cursor;
    CognitionObservation resumed_focus;
    std::size_t continuation_calls=0;
    do {
      resumed_focus=graph.observe_focused_cognition(
          compact,positions,shared_depths,traversal_cursor,1,1);
      CHECK(++continuation_calls<64);
    } while(!resumed_focus.complete);
    std::vector<std::string> expected_nodes,actual_nodes;
    std::vector<std::string> expected_relations,actual_relations;
    for(const auto& node:unbounded_focus.nodes) expected_nodes.push_back(node.id);
    for(const auto& node:resumed_focus.nodes) actual_nodes.push_back(node.id);
    for(const auto& relation:unbounded_focus.relations) expected_relations.push_back(relation.id);
    for(const auto& relation:resumed_focus.relations) actual_relations.push_back(relation.id);
    std::sort(expected_nodes.begin(),expected_nodes.end());
    std::sort(actual_nodes.begin(),actual_nodes.end());
    std::sort(expected_relations.begin(),expected_relations.end());
    std::sort(actual_relations.begin(),actual_relations.end());
    CHECK(actual_nodes==expected_nodes);
    CHECK(actual_relations==expected_relations);
    CHECK(resumed_focus.complete);
    CHECK(traversal_cursor.complete);
    CHECK(continuation_calls>1);

    // Completed snapshots from one-shot and sliced traversal must be identical
    // when they describe the same graph state and logical Focus.
    SnapshotDocument one_shot_manifest, sliced_manifest;
    std::string traversal_snapshot_error;
    CHECK(make_completed_focus_snapshot_document(graph,unbounded_focus,
        "focus-geometry-v1","observation-v1","traversal-context",77,
        &one_shot_manifest,traversal_snapshot_error));
    CHECK(make_completed_focus_snapshot_document(graph,resumed_focus,
        "focus-geometry-v1","observation-v1","traversal-context",77,
        &sliced_manifest,traversal_snapshot_error));
    FocusEvaluationSnapshotDiff traversal_diff;
    CHECK(diff_completed_focus_snapshots(one_shot_manifest,sliced_manifest,
        &traversal_diff,traversal_snapshot_error));
    CHECK(!traversal_diff.focus_changed);
    CHECK(!traversal_diff.graph_changed);
    CHECK(validate_focus_snapshot_against_graph(one_shot_manifest,graph,
        traversal_snapshot_error));

    CHECK(std::adjacent_find(actual_nodes.begin(),actual_nodes.end())==actual_nodes.end());
    CHECK(std::adjacent_find(actual_relations.begin(),actual_relations.end())==actual_relations.end());

    // A zero budget must not even start a previously untouched root.
    FocusTraversalCursor untouched_cursor;
    const auto untouched=graph.observe_focused_cognition(
        compact,positions,shared_depths,untouched_cursor,0,0);
    CHECK(!untouched.complete);
    CHECK(!untouched_cursor.initialized);
    CHECK(untouched_cursor.next_root_index==0);
    CHECK(untouched_cursor.active_root_id.empty());
    CHECK(untouched_cursor.frontier.empty());

    // A zero budget is a true no-op for an initialized continuation cursor.
    const auto saved_frontier_size=traversal_cursor.frontier.size();
    const auto saved_next_root=traversal_cursor.next_root_index;
    const auto zero_budget=graph.observe_focused_cognition(
        compact,positions,shared_depths,traversal_cursor,0,0);
    CHECK(zero_budget.complete);
    CHECK(traversal_cursor.frontier.size()==saved_frontier_size);
    CHECK(traversal_cursor.next_root_index==saved_next_root);

    // Graph mutation invalidates a partial cursor without mutating that cursor.
    FocusTraversalCursor stale_cursor;
    const auto partial_before_mutation=graph.observe_focused_cognition(
        compact,positions,shared_depths,stale_cursor,1,1);
    CHECK(!partial_before_mutation.complete);
    const auto stale_version=stale_cursor.state_version;
    const auto stale_frontier_size=stale_cursor.frontier.size();
    SemanticDelta graph_change;
    graph_change.delta_id="spatial-cursor-invalidation";
    graph_change.idempotency_key="spatial-cursor-invalidation";
    graph_change.expected_state_version=graph.state_version();
    graph_change.nodes={{{"spatial-cursor-version-bump",NodeKind::Entity,
        "cursor invalidation probe",{}}}};
    CHECK(graph.apply(graph_change).committed);
    const auto invalidated=graph.observe_focused_cognition(
        compact,positions,shared_depths,stale_cursor,10,10);
    CHECK(invalidated.error=="FOCUS_CURSOR_STALE_STATE_VERSION");
    CHECK(stale_cursor.state_version==stale_version);
    CHECK(stale_cursor.frontier.size()==stale_frontier_size);

    // A zero node budget defers all work without hiding any cluster from the
    // logical region. The geometric region is not a budget-sized prefix.
    const auto no_node_budget =
        graph.observe_focused_cognition(expanded,positions,deeper,0,20);
    CHECK(no_node_budget.focus_work.budget_exhausted);
    CHECK(no_node_budget.nodes.empty());
    CHECK(no_node_budget.focus_work.nodes_processed==0);
    CHECK(no_node_budget.logical_region_cluster_ids.size()==3);
    CHECK(no_node_budget.processed_region_cluster_ids.empty());
    CHECK(no_node_budget.deferred_region_cluster_ids.size()==3);

    // Persist region, positions and depths as independent journal event families.
    SemanticDelta region_delta;region_delta.delta_id="spatial-region-update";
    region_delta.idempotency_key="spatial-region-update";
    region_delta.expected_state_version=graph.state_version();
    const FocusRegion durable_region{{0.0,0.0,0.0},{2.0,2.0,2.0}};
    CHECK(graph.append_focus_region_update(region_delta,durable_region,spatial_error));
    CHECK(graph.apply(region_delta).committed);

    SemanticDelta position_delta;position_delta.delta_id="spatial-position-update";
    position_delta.idempotency_key="spatial-position-update";
    position_delta.expected_state_version=graph.state_version();
    CHECK(graph.append_cluster_spatial_position_update(position_delta,{"spatial-a",{-0.5,0.0,0.0}},spatial_error));
    CHECK(graph.append_cluster_spatial_position_update(position_delta,{"spatial-b",{0.5,0.0,0.0}},spatial_error));
    CHECK(graph.append_cluster_spatial_position_update(position_delta,{"spatial-c",{5.0,0.0,0.0}},spatial_error));
    CHECK(graph.apply(position_delta).committed);

    SemanticDelta depth_delta;depth_delta.delta_id="spatial-depth-update";
    depth_delta.idempotency_key="spatial-depth-update";
    depth_delta.expected_state_version=graph.state_version();
    CHECK(graph.append_cluster_focus_depth_update(depth_delta,{"spatial-a",1},spatial_error));
    CHECK(graph.append_cluster_focus_depth_update(depth_delta,{"spatial-b",0},spatial_error));
    CHECK(graph.append_cluster_focus_depth_update(depth_delta,{"spatial-nested",1},spatial_error));
    CHECK(graph.apply(depth_delta).committed);

    SemanticDelta move_delta;move_delta.delta_id="spatial-region-move";
    move_delta.idempotency_key="spatial-region-move";
    move_delta.expected_state_version=graph.state_version();
    CHECK(graph.append_focus_region_move(move_delta,{0.25,0.0,0.0},spatial_error));
    CHECK(graph.apply(move_delta).committed);
    SemanticDelta resize_delta;resize_delta.delta_id="spatial-region-resize";
    resize_delta.idempotency_key="spatial-region-resize";
    resize_delta.expected_state_version=graph.state_version();
    CHECK(graph.append_focus_region_resize(resize_delta,{1.5,2.0,2.0},spatial_error));
    CHECK(graph.apply(resize_delta).committed);

    const auto durable_observation=graph.observe_focused_cognition(20,20);
    CHECK(durable_observation.focus_region.has_value());
    CHECK(durable_observation.focus_region->center.x==0.25);
    CHECK(durable_observation.focus_region->half_extents.x==1.5);
    CHECK(durable_observation.cluster_spatial_positions.size()==2);
    CHECK(durable_observation.cluster_focus_depths.size()==3);
    CHECK(durable_observation.logical_region_cluster_ids.size()==2);
    CHECK(durable_observation.processed_region_cluster_ids.size()==2);
    CHECK(durable_observation.deferred_region_cluster_ids.empty());
    // The x-axis index excludes the third cluster before exact 3D checks.
    CHECK(durable_observation.focus_work.position_checks==2);
    CHECK(durable_observation.focus_work.clusters_considered==2);

    // Updating a persisted position invalidates and rebuilds the derived index.
    SemanticDelta bring_inside;bring_inside.delta_id="spatial-index-bring-inside";
    bring_inside.idempotency_key="spatial-index-bring-inside";
    bring_inside.expected_state_version=graph.state_version();
    CHECK(graph.append_cluster_spatial_position_update(
        bring_inside,{"spatial-c",{1.0,0.0,0.0}},spatial_error));
    CHECK(graph.apply(bring_inside).committed);
    const auto expanded_index_view=graph.observe_focused_cognition(20,20);
    CHECK(expanded_index_view.logical_region_cluster_ids.size()==3);
    CHECK(expanded_index_view.focus_work.position_checks==3);

    SemanticDelta move_outside;move_outside.delta_id="spatial-index-move-outside";
    move_outside.idempotency_key="spatial-index-move-outside";
    move_outside.expected_state_version=graph.state_version();
    CHECK(graph.append_cluster_spatial_position_update(
        move_outside,{"spatial-c",{5.0,0.0,0.0}},spatial_error));
    CHECK(graph.apply(move_outside).committed);
    const auto restored_index_view=graph.observe_focused_cognition(20,20);
    CHECK(restored_index_view.logical_region_cluster_ids.size()==2);
    CHECK(restored_index_view.focus_work.position_checks==2);
  } // Close the journal writer before opening a second reader.

  {
    // Restart replay restores all independent spatial state.
      ContextGraph replayed(path);CHECK(replayed.open());
      const auto replayed_view=replayed.observe_focused_cognition(20,20);
      CHECK(replayed_view.focus_region.has_value());
      CHECK(replayed_view.focus_region->center.x==0.25);
      CHECK(replayed_view.focus_region->half_extents.x==1.5);
      CHECK(replayed_view.cluster_spatial_positions.size()==2);
      CHECK(replayed_view.cluster_focus_depths.size()==3);
      CHECK(replayed_view.logical_region_cluster_ids.size()==2);

      // CGS2 snapshot restore retains the same state and accepts legacy CGS1.
      ContextGraphSnapshotDomain domain(replayed);
      std::string snapshot_payload;
      CHECK(domain.capture(&snapshot_payload,&spatial_error));
      CHECK(snapshot_payload.rfind("CGS2 ",0)==0);
      const auto restore_path=(root/"spatial-focus-restore.sgj").string();
      ContextGraph restored(restore_path);CHECK(restored.open());
      ContextGraphSnapshotDomain restored_domain(restored);
      auto prepared=restored_domain.prepare_restore(snapshot_payload,&spatial_error);
      CHECK(prepared!=nullptr);
      CHECK(prepared->commit());
      const auto restored_view=restored.observe_focused_cognition(20,20);
      CHECK(restored_view.focus_region.has_value());
      CHECK(restored_view.cluster_spatial_positions.size()==2);
      CHECK(restored_view.cluster_focus_depths.size()==3);
      CHECK(restored_view.logical_region_cluster_ids.size()==2);
    }
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
  std::string classified_cluster_id, weighted_cluster_id;
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
        second_cluster_id,error,0.73));
    weighted_cluster_id=second_cluster_id;
    CHECK(second_cluster_id!=classified_cluster_id);
    CHECK(std::any_of(delta.relations.begin(),delta.relations.end(),
        [&](const auto& item){return item.relation.from==second_cluster_id &&
          item.relation.to=="claim-a" && item.relation.weight==0.73;}));
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
    const auto classified_relations = graph.relations();
    CHECK(std::any_of(classified_relations.begin(),classified_relations.end(),
        [&](const auto& item){return item.second.from=="input-root" &&
          item.second.to==classified_cluster_id && item.second.kind==RelationKind::Contains;}));
    CHECK(std::any_of(classified_relations.begin(),classified_relations.end(),
        [&](const auto& item){return item.second.from==classified_cluster_id &&
          item.second.to=="claim-a" && item.second.kind==RelationKind::Contains;}));
  }
  {
    ContextGraph recovered(cluster_path); CHECK(recovered.open());
    CHECK(recovered.nodes().count(classified_cluster_id)==1);
    const auto recovered_relations = recovered.relations();
    CHECK(std::any_of(recovered_relations.begin(),recovered_relations.end(),
        [&](const auto& item){return item.second.from==weighted_cluster_id &&
          item.second.to=="claim-a" && item.second.weight==0.73;}));
    CHECK(std::any_of(recovered_relations.begin(),recovered_relations.end(),
        [&](const auto& item){return item.second.from==classified_cluster_id &&
          item.second.to=="claim-a";}));
  }

  // Open-world admission keeps unknown information without inventing
  // semantic relations; later classification grows its memberships.
  const auto open_world_path=(root/"open-world-chaos-membership.sgj").string();
  std::string unresolved_root_id, unresolved_edge_id;
  {
    ContextGraph graph(open_world_path);CHECK(graph.open());
    SemanticDelta admit;admit.delta_id="open-world-admit";
    admit.idempotency_key="open-world-admit";admit.expected_state_version=0;
    admit.nodes={{{"unclassified-note",NodeKind::Entity,"短い未分類の発言",{}}}};
    std::string error;
    CHECK(graph.append_unresolved_membership(admit,"unclassified-note",
        unresolved_root_id,unresolved_edge_id,error));
    CHECK(unresolved_root_id=="surge:chaos:unresolved-root");
    CHECK(graph.apply(admit).committed);
    CHECK(graph.nodes().at(unresolved_root_id).kind==NodeKind::ChaosCluster);
    CHECK(graph.cluster_membership_reference_count("unclassified-note")==1);
    SemanticDelta classify;classify.delta_id="open-world-classify";
    classify.idempotency_key="open-world-classify";classify.expected_state_version=1;
    classify.nodes={{{"conversation-topic",NodeKind::Cluster,"会話トピック",{}}}};
    std::string assigned_cluster;
    CHECK(graph.append_cluster_membership(classify,"conversation-topic",
        "ACKNOWLEDGEMENT","turn-17","unclassified-note","later-classification",
        assigned_cluster,error));
    classify.remove_relation_ids.push_back(unresolved_edge_id);
    CHECK(graph.apply(classify).committed);
    CHECK(graph.cluster_membership_reference_count("unclassified-note")==1);
    const auto cognition = graph.observe_cognition();
    CHECK(cognition.state_version==graph.state_version());
    CHECK(cognition.unresolved_member_ids.empty());
    CHECK(std::any_of(cognition.memberships.begin(),cognition.memberships.end(),
        [&](const auto& item){return item.member_node_id=="unclassified-note" &&
          item.cluster_node_id==assigned_cluster && item.upstream_weight==1.0;}));
    CHECK(std::any_of(cognition.nodes.begin(),cognition.nodes.end(),
        [](const auto& node){return node.id=="unclassified-note";}));
    CHECK(graph.relations().count(unresolved_edge_id)==0);
    const auto open_world_relations = graph.relations();
    CHECK(std::any_of(open_world_relations.begin(),open_world_relations.end(),
        [&](const auto& item){return item.second.kind==RelationKind::Contains &&
          item.second.from==assigned_cluster && item.second.to=="unclassified-note";}));
  }
  {
    ContextGraph recovered(open_world_path);CHECK(recovered.open());
    CHECK(recovered.nodes().count("unclassified-note")==1);
    CHECK(recovered.nodes().count(unresolved_root_id)==1);
    CHECK(recovered.cluster_membership_reference_count("unclassified-note")==1);
    CHECK(recovered.relations().count(unresolved_edge_id)==0);
  }

  // Candidate recall is retrieval-only: its score must never become a
  // membership weight, and feedback must not rewrite existing memberships.
  {
    const auto recall_path=(root/"cognition-recall-feedback.sgj").string();
    ContextGraph graph(recall_path);CHECK(graph.open());
    SemanticDelta seed;seed.delta_id="cognition-seed";
    seed.idempotency_key="cognition-seed";seed.expected_state_version=0;
    seed.nodes={
      {{"recall-cluster-low",NodeKind::Cluster,"CLASS=TOPIC;PARTITION=LOW",{}}},
      {{"recall-cluster-high",NodeKind::Cluster,"CLASS=TOPIC;PARTITION=HIGH",{}}},
      {{"recall-cluster-related",NodeKind::Cluster,"CLASS=RELATED",{}}},
      {{"recall-input",NodeKind::Entity,"incoming stream event",{}}},
      {{"recall-member-low",NodeKind::Evidence,"agriculture field irrigation",{}}},
      {{"recall-member-high",NodeKind::Evidence,"agriculture field irrigation crop",{}}},
      {{"recall-member-related",NodeKind::Evidence,"urban planning transport",{}}}
    };
    seed.relations={
      {{"recall-low-edge",RelationKind::Contains,"recall-cluster-low","recall-member-low",0.2}},
      {{"recall-high-edge",RelationKind::Contains,"recall-cluster-high","recall-member-high",0.9}},
      {{"recall-cluster-association",RelationKind::AgreesWith,
        "recall-cluster-low","recall-cluster-high",0.8}},
      {{"recall-cluster-synthesis",RelationKind::DerivedFrom,
        "recall-cluster-low","recall-cluster-related",0.75}},
      {{"recall-related-edge",RelationKind::Contains,
        "recall-cluster-related","recall-member-related",0.7}}
    };
    CHECK(graph.apply(seed).committed);
    const auto memberships_before=graph.observe_cognition().memberships;
    const auto candidates=graph.recall_cognition_candidates(
        "recall-input",{"agriculture","irrigation"},8);
    CHECK(candidates.size()==3);
    CHECK(candidates.front().cluster_id=="recall-cluster-high");
    CHECK(std::any_of(candidates.begin(), candidates.end(), [](const auto& candidate) {
      return candidate.cluster_id=="recall-cluster-related" &&
          candidate.recall_score > 0.0 &&
          std::find(candidate.supporting_member_ids.begin(),
              candidate.supporting_member_ids.end(), "recall-member-related") !=
              candidate.supporting_member_ids.end();
    }));
    CHECK(candidates.front().recall_score>candidates.back().recall_score);
    CHECK(candidates.front().memory_state_version==graph.state_version());
    CHECK(graph.relations().at("recall-high-edge").weight==0.9);
    CHECK(graph.relations().at("recall-low-edge").weight==0.2);
    const auto version_before_feedback=graph.state_version();
    SemanticDelta feedback_delta;feedback_delta.delta_id="cognition-feedback";
    feedback_delta.idempotency_key="cognition-feedback";
    feedback_delta.expected_state_version=version_before_feedback;
    CognitionFeedback feedback;
    feedback.feedback_id="external-review-1";
    feedback.candidate_cluster_id="recall-cluster-high";
    feedback.input_member_id="recall-input";
    feedback.outcome="CONFIRMED";
    feedback.producer_id="external-model";
    feedback.producer_version="1";
    feedback.policy_version="review-policy-1";
    feedback.recorded_at="2026-11-22T00:00:00Z";
    feedback.notes="externally confirmed";
    std::string feedback_error;
    CHECK(graph.append_cognition_feedback(feedback_delta,feedback,
        "feedback-evidence-1","feedback-about-1",feedback_error));
    CHECK(graph.observe_cognition().memberships.size()==memberships_before.size());
    CHECK(graph.apply(feedback_delta).committed);
    const auto memberships_after=graph.observe_cognition().memberships;
    CHECK(memberships_after.size()==memberships_before.size());
    CHECK(graph.relations().at("recall-high-edge").weight==0.9);
    CHECK(graph.relations().at("recall-low-edge").weight==0.2);
    CHECK(graph.cluster_membership_reference_count("recall-member-low")==1);
    CHECK(graph.cluster_membership_reference_count("recall-member-high")==1);
    CognitionFeedback invalid=feedback;invalid.outcome="UNSUPPORTED";
    SemanticDelta invalid_delta;
    CHECK(!graph.append_cognition_feedback(invalid_delta,invalid,
        "feedback-invalid","feedback-invalid-edge",feedback_error));
    CHECK(invalid_delta.nodes.empty()&&invalid_delta.relations.empty());
  }

  // Focus gates candidate recall by the actual work slice while preserving
  // every cluster in the logical 3D region for later processing.
  {
    const auto path=(root/"focused-candidate-recall.sgj").string();
    ContextGraph graph(path);CHECK(graph.open());
    SemanticDelta seed;seed.delta_id="focused-recall-seed";
    seed.idempotency_key="focused-recall-seed";seed.expected_state_version=0;
    seed.nodes={{{"focus-recall-a",NodeKind::Cluster,"A",{} }},
                {{"focus-recall-b",NodeKind::Cluster,"B",{} }},
                {{"focus-recall-evidence-a",NodeKind::Evidence,"river habitat",{} }},
                {{"focus-recall-evidence-b",NodeKind::Evidence,"river floodplain",{} }}};
    seed.relations={{{"focus-recall-a-member",RelationKind::Contains,
                      "focus-recall-a","focus-recall-evidence-a",0.8}},
                    {{"focus-recall-b-member",RelationKind::Contains,
                      "focus-recall-b","focus-recall-evidence-b",0.7}}};
    CHECK(graph.apply(seed).committed);
    SemanticDelta focus_delta;focus_delta.delta_id="focused-recall-settings";
    focus_delta.idempotency_key="focused-recall-settings";
    focus_delta.expected_state_version=graph.state_version();
    std::string focus_error;
    CHECK(graph.append_focus_region_update(focus_delta,
        {{0.0,0.0,0.0},{2.0,2.0,2.0}},focus_error));
    CHECK(graph.append_cluster_spatial_position_update(focus_delta,
        {"focus-recall-a",{-1.0,0.0,0.0}},focus_error));
    CHECK(graph.append_cluster_spatial_position_update(focus_delta,
        {"focus-recall-b",{1.0,0.0,0.0}},focus_error));
    CHECK(graph.append_cluster_focus_depth_update(focus_delta,
        {"focus-recall-a",1},focus_error));
    CHECK(graph.append_cluster_focus_depth_update(focus_delta,
        {"focus-recall-b",1},focus_error));
    CHECK(graph.apply(focus_delta).committed);
    const auto batch=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},8,2,8);
    CHECK(batch.logical_region_cluster_ids.size()==2);
    CHECK(batch.processed_region_cluster_ids.size()==1);
    CHECK(batch.deferred_region_cluster_ids.size()==1);
    CHECK(batch.candidates.size()==1);
    CHECK(batch.deferred_candidates.size()==1);
    CHECK(batch.focus_work.budget_exhausted);
    CHECK(batch.candidates.front().cluster_id==batch.processed_region_cluster_ids.front());
    CHECK(batch.deferred_candidates.front().cluster_id==batch.deferred_region_cluster_ids.front());

    // Depth zero exposes the root but not its supporting Evidence. Such a
    // candidate must remain deferred rather than leaking a global recall score.
    SemanticDelta shallow_focus;shallow_focus.delta_id="focused-recall-shallow";
    shallow_focus.idempotency_key="focused-recall-shallow";
    shallow_focus.expected_state_version=graph.state_version();
    CHECK(graph.append_cluster_focus_depth_update(shallow_focus,
        {"focus-recall-a",0},focus_error));
    CHECK(graph.apply(shallow_focus).committed);
    const auto shallow=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},8,16,32);
    CHECK(shallow.candidates.size()==1);
    CHECK(shallow.deferred_candidates.size()==1);
    CHECK(shallow.candidates.front().cluster_id=="focus-recall-b");
    CHECK(shallow.deferred_candidates.front().cluster_id=="focus-recall-a");

    // A caller-owned cursor advances by deterministic Focus root boundaries.
    FocusedCognitionCursor cursor;
    const auto first_page=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},cursor,8,2,8);
    CHECK(first_page.error.empty());
    CHECK(first_page.logical_region_cluster_ids.size()==2);
    CHECK(first_page.processed_region_cluster_ids.size()==1);
    CHECK(first_page.next_root_index==1);
    CHECK(!first_page.complete);
    CHECK(first_page.processed_region_cluster_ids.front()=="focus-recall-a");
    const auto second_page=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},cursor,8,2,8);
    CHECK(second_page.error.empty());
    CHECK(second_page.processed_region_cluster_ids.size()==1);
    CHECK(second_page.processed_region_cluster_ids.front()=="focus-recall-b");
    CHECK(second_page.candidates.size()==1);
    CHECK(second_page.candidates.front().cluster_id=="focus-recall-b");
    CHECK(second_page.complete);
    CHECK(second_page.next_root_index==2);

    // Restore full support visibility so this pagination assertion isolates
    // the output cap rather than the earlier depth-zero deferral case.
    SemanticDelta restore_candidate_depth;
    restore_candidate_depth.delta_id="focused-recall-restore-depth";
    restore_candidate_depth.idempotency_key="focused-recall-restore-depth";
    restore_candidate_depth.expected_state_version=graph.state_version();
    CHECK(graph.append_cluster_focus_depth_update(restore_candidate_depth,
        {"focus-recall-a",1},focus_error));
    CHECK(graph.apply(restore_candidate_depth).committed);

    // A candidate output cap must not consume later Focus roots. Both roots
    // fit in the observation budget, but the caller asks for one candidate
    // per page and must be able to retrieve the second root on the next call.
    FocusedCognitionCursor limited_cursor;
    const auto limited_first=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},limited_cursor,1,16,32);
    CHECK(limited_first.error.empty());
    CHECK(limited_first.candidates.size()==1);
    CHECK(limited_first.candidates.front().cluster_id=="focus-recall-a");
    CHECK(!limited_first.complete);
    CHECK(limited_first.next_root_index==1);
    const auto limited_second=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},limited_cursor,1,16,32);
    CHECK(limited_second.error.empty());
    CHECK(limited_second.candidates.size()==1);
    CHECK(limited_second.candidates.front().cluster_id=="focus-recall-b");
    CHECK(limited_second.complete);
    CHECK(limited_second.next_root_index==2);

    // A zero candidate cap is a no-op and leaves the cursor untouched.
    FocusedCognitionCursor zero_cap_cursor;
    const auto zero_cap=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},zero_cap_cursor,0,16,32);
    CHECK(zero_cap.candidates.empty());
    CHECK(zero_cap.next_root_index==0);
    const auto after_zero_cap=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},zero_cap_cursor,1,16,32);
    CHECK(after_zero_cap.candidates.size()==1);
    CHECK(after_zero_cap.candidates.front().cluster_id=="focus-recall-a");
    CHECK(after_zero_cap.next_root_index==1);

    FocusedCognitionCursor query_cursor;
    const auto query_page=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},query_cursor,8,2,8);
    CHECK(query_page.error.empty());
    const auto query_mismatch=graph.recall_focused_cognition_candidates(
        "incoming-input",{"flood"},query_cursor,8,2,8);
    CHECK(query_mismatch.error=="FOCUS_CURSOR_QUERY_MISMATCH");

    FocusedCognitionCursor stale_cursor;
    const auto stale_seed=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},stale_cursor,8,2,8);
    CHECK(stale_seed.error.empty());
    SemanticDelta change_focus;change_focus.delta_id="focused-recall-cursor-stale";
    change_focus.idempotency_key="focused-recall-cursor-stale";
    change_focus.expected_state_version=graph.state_version();
    CHECK(graph.append_cluster_focus_depth_update(change_focus,
        {"focus-recall-b",0},focus_error));
    CHECK(graph.apply(change_focus).committed);
    const auto stale_page=graph.recall_focused_cognition_candidates(
        "incoming-input",{"river"},stale_cursor,8,2,8);
    CHECK(stale_page.error=="FOCUS_CURSOR_STALE_STATE_VERSION");
  }

  // Canonical Graph Digest is independent of Focus geometry, but changes
  // when canonical cognition nodes or relations change.
  {
    ContextGraph digest_graph((root/"canonical-graph-digest.sgj").string());
    CHECK(digest_graph.open());
    SemanticDelta seed;
    seed.delta_id="canonical-digest-seed";
    seed.idempotency_key=seed.delta_id;
    seed.expected_state_version=digest_graph.state_version();
    seed.nodes={{{"digest-root",NodeKind::Cluster,"digest root",{}}}};
    CHECK(digest_graph.apply(seed).committed);
    ContextGraphSnapshotDomain digest_domain(digest_graph);
    std::string digest_before, digest_error;
    CHECK(digest_domain.canonical_digest(&digest_before,&digest_error));
    SemanticDelta focus_move;
    focus_move.delta_id="canonical-digest-focus";
    focus_move.idempotency_key=focus_move.delta_id;
    focus_move.expected_state_version=digest_graph.state_version();
    CHECK(digest_graph.append_focus_region_update(focus_move,
        FocusRegion{{0.0,0.0,0.0},{2.0,2.0,2.0}},digest_error));
    CHECK(digest_graph.apply(focus_move).committed);
    std::string digest_after_focus;
    CHECK(digest_domain.canonical_digest(&digest_after_focus,&digest_error));
    CHECK(digest_after_focus==digest_before);
    SemanticDelta graph_change;
    graph_change.delta_id="canonical-digest-node";
    graph_change.idempotency_key=graph_change.delta_id;
    graph_change.expected_state_version=digest_graph.state_version();
    graph_change.nodes={{{"digest-member",NodeKind::Evidence,"new evidence",{}}}};
    graph_change.relations={{{"digest-membership",RelationKind::Contains,
        "digest-root","digest-member"}}};
    CHECK(digest_graph.apply(graph_change).committed);
    std::string digest_after_graph;
    CHECK(digest_domain.canonical_digest(&digest_after_graph,&digest_error));
    CHECK(digest_after_graph!=digest_before);
  }

  // Completed Focus evaluations can be promoted to snapshot metadata only
  // when every logical root is processed and provenance is present.
  {
    CognitionObservation observation;
    observation.state_version=7;
    observation.complete=true;
    observation.logical_region_cluster_ids={"root-a","root-b"};
    observation.processed_region_cluster_ids={"root-b","root-a"};
    observation.nodes={{"root-a",NodeKind::Cluster,"A",{}},
                      {"root-b",NodeKind::Cluster,"B",{}},
                      {"member-a",NodeKind::Evidence,"member",{}}};
    observation.relations={{"rel-a",RelationKind::Contains,"root-a","member-a",1.0}};
    FocusEvaluationSnapshotMetadata metadata;
    metadata.evaluated_state_version=7;
    metadata.focus_digest="focus-digest";
    metadata.graph_digest="graph-v1";
    metadata.observation_digest="observation-digest";
    std::string snapshot_error;
    CHECK(validate_completed_focus_evaluation(observation,metadata,snapshot_error));
    CHECK(snapshot_error.empty());
    SnapshotDocument first_manifest;
    CHECK(make_completed_focus_snapshot_document(
        observation,metadata,"test-context",12,&first_manifest,snapshot_error));
    CHECK(first_manifest.schema_id=="surge.focus-evaluation.v1");
    SnapshotLimits snapshot_limits;
    std::string encoded_snapshot;
    CHECK(SnapshotCodec::encode(first_manifest,snapshot_limits,
                                &encoded_snapshot,&snapshot_error));
    SnapshotDocument decoded_snapshot;
    CHECK(SnapshotCodec::decode(encoded_snapshot,snapshot_limits,
                                 &decoded_snapshot,&snapshot_error));

    // Persist the encoded Focus manifest through the real durable store and
    // restore it before decoding. This exercises the file boundary, not only
    // the in-memory codec.
    const auto manifest_path=(root/"focus-evaluation-manifest.snapshot").string();
    SnapshotStore manifest_store(manifest_path);
    CHECK(manifest_store.save(encoded_snapshot,&snapshot_error));
    const auto stored_manifest=manifest_store.load();
    CHECK(stored_manifest.ok && !stored_manifest.used_backup);
    CHECK(stored_manifest.bytes==encoded_snapshot);
    SnapshotDocument restored_manifest;
    CHECK(SnapshotCodec::decode(stored_manifest.bytes,snapshot_limits,
                                &restored_manifest,&snapshot_error));
    CHECK(restored_manifest.context_id=="test-context");
    CHECK(restored_manifest.state_version==7);

    FocusEvaluationSnapshotDiff no_change;
    CHECK(diff_completed_focus_snapshots(first_manifest,decoded_snapshot,
                                          &no_change,snapshot_error));
    CHECK(no_change.added_node_ids.empty());
    CHECK(no_change.removed_node_ids.empty());

    auto later_observation=observation;
    later_observation.state_version=8;
    later_observation.nodes.push_back({"member-b",NodeKind::Evidence,"new member",{}});
    auto later_metadata=metadata;
    later_metadata.evaluated_state_version=8;
    later_metadata.graph_digest="graph-v2";
    later_metadata.observation_digest="later-observation";
    SnapshotDocument later_manifest;
    CHECK(make_completed_focus_snapshot_document(
        later_observation,later_metadata,"test-context",13,
        &later_manifest,snapshot_error));
    FocusEvaluationSnapshotDiff changed;
    CHECK(diff_completed_focus_snapshots(first_manifest,later_manifest,
                                          &changed,snapshot_error));
    CHECK(changed.added_node_ids.size()==1);
    CHECK(changed.added_node_ids.front()=="member-b");
    CHECK(changed.graph_changed);
    CHECK(!changed.focus_changed);

    // Graph content can change without changing any visible IDs. The
    // Focus-independent graph digest must still surface that mutation.
    auto graph_only_observation=observation;
    graph_only_observation.state_version=8;
    auto graph_only_metadata=metadata;
    graph_only_metadata.evaluated_state_version=8;
    graph_only_metadata.graph_digest="graph-v2";
    graph_only_metadata.observation_digest="graph-content-updated";
    SnapshotDocument graph_only_manifest;
    CHECK(make_completed_focus_snapshot_document(
        graph_only_observation,graph_only_metadata,"test-context",13,
        &graph_only_manifest,snapshot_error));
    CHECK(diff_completed_focus_snapshots(first_manifest,graph_only_manifest,
                                          &changed,snapshot_error));
    CHECK(changed.graph_changed);
    CHECK(!changed.focus_changed);
    CHECK(changed.added_node_ids.empty());
    CHECK(changed.removed_node_ids.empty());

    auto other_context=later_manifest;
    other_context.context_id="other-context";
    CHECK(!diff_completed_focus_snapshots(first_manifest,other_context,
                                           &changed,snapshot_error));
    CHECK(snapshot_error=="FOCUS_SNAPSHOT_DIFF_CONTEXT_MISMATCH");

    auto other_focus=later_manifest;
    // Rebuild a valid manifest under another Focus provenance.
    auto focus_only_observation=observation;
    focus_only_observation.state_version=8;
    auto other_focus_metadata=metadata;
    other_focus_metadata.evaluated_state_version=8;
    other_focus_metadata.focus_digest="different-focus";
    other_focus_metadata.observation_digest="focus-only-observation";
    CHECK(make_completed_focus_snapshot_document(
        focus_only_observation,other_focus_metadata,"test-context",13,
        &other_focus,snapshot_error));
    CHECK(diff_completed_focus_snapshots(first_manifest,other_focus,
                                          &changed,snapshot_error));
    CHECK(changed.focus_changed);
    CHECK(!changed.graph_changed);
    CHECK(changed.added_node_ids.empty());
    CHECK(changed.added_relation_ids.empty());

    // Malformed and duplicate-ID payloads must never be interpreted as a
    // valid graph delta, even when wrapped in a valid SnapshotDocument.
    auto malformed_manifest=first_manifest;
    malformed_manifest.payload.pop_back();
    CHECK(!diff_completed_focus_snapshots(first_manifest,malformed_manifest,
                                           &changed,snapshot_error));
    CHECK(snapshot_error=="FOCUS_SNAPSHOT_DIFF_MANIFEST_INVALID");
    auto duplicate_manifest=first_manifest;
    duplicate_manifest.payload="FES1";
    focus_snapshot_detail::append_string(duplicate_manifest.payload,"focus-digest");
    focus_snapshot_detail::append_string(duplicate_manifest.payload,"graph-v1");
    focus_snapshot_detail::append_string(duplicate_manifest.payload,"observation-digest");
    focus_snapshot_detail::append_set(duplicate_manifest.payload,{"root-a","root-b"});
    focus_snapshot_detail::append_set(duplicate_manifest.payload,{"member-a","member-a"});
    focus_snapshot_detail::append_set(duplicate_manifest.payload,{"rel-a"});
    CHECK(!diff_completed_focus_snapshots(first_manifest,duplicate_manifest,
                                           &changed,snapshot_error));
    CHECK(snapshot_error=="FOCUS_SNAPSHOT_DIFF_MANIFEST_INVALID");

    FocusSnapshotHistory timeline;
    CHECK(timeline.append(first_manifest,snapshot_error));
    CHECK(timeline.append(later_manifest,snapshot_error));
    CHECK(timeline.size()==2);
    FocusEvaluationSnapshotDiff historical_diff;
    CHECK(timeline.diff(0,1,&historical_diff,snapshot_error));
    CHECK(historical_diff.graph_changed);
    CHECK(!historical_diff.focus_changed);
    CHECK(!timeline.append(other_context,snapshot_error));
    CHECK(snapshot_error=="FOCUS_SNAPSHOT_HISTORY_CONTEXT_MISMATCH");
    auto regressed_manifest=first_manifest;
    regressed_manifest.state_version=6;
    CHECK(!timeline.append(regressed_manifest,snapshot_error));
    CHECK(snapshot_error=="FOCUS_SNAPSHOT_HISTORY_WATERMARK_REGRESSION");

    auto partial=observation;
    partial.complete=false;
    CHECK(!validate_completed_focus_evaluation(partial,metadata,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_INCOMPLETE");

    auto deferred=observation;
    deferred.deferred_region_cluster_ids={"root-b"};
    CHECK(!validate_completed_focus_evaluation(deferred,metadata,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_DEFERRED_ROOTS");

    auto mismatched=observation;
    mismatched.processed_region_cluster_ids={"root-a"};
    CHECK(!validate_completed_focus_evaluation(mismatched,metadata,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_ROOT_SET_MISMATCH");

    auto stale=metadata;
    stale.evaluated_state_version=6;
    CHECK(!validate_completed_focus_evaluation(observation,stale,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_STATE_VERSION_MISMATCH");

    auto missing_provenance=metadata;
    missing_provenance.focus_digest.clear();
    CHECK(!validate_completed_focus_evaluation(observation,missing_provenance,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_PROVENANCE_MISSING");

    auto duplicate_roots=observation;
    duplicate_roots.logical_region_cluster_ids.push_back("root-a");
    CHECK(!validate_completed_focus_evaluation(duplicate_roots,metadata,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_DUPLICATE_ROOT");

    auto duplicate_nodes=observation;
    duplicate_nodes.nodes.push_back(duplicate_nodes.nodes.front());
    CHECK(!validate_completed_focus_evaluation(duplicate_nodes,metadata,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_DUPLICATE_OR_INVALID_NODE");

    auto duplicate_relations=observation;
    duplicate_relations.relations.push_back(duplicate_relations.relations.front());
    CHECK(!validate_completed_focus_evaluation(duplicate_relations,metadata,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_DUPLICATE_OR_INVALID_RELATION");

    auto missing_root_node=observation;
    missing_root_node.nodes.erase(missing_root_node.nodes.begin());
    CHECK(!validate_completed_focus_evaluation(missing_root_node,metadata,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_ROOT_NODE_MISSING");

    auto missing_relation_endpoint=observation;
    missing_relation_endpoint.relations.front().to="absent-node";
    CHECK(!validate_completed_focus_evaluation(missing_relation_endpoint,metadata,snapshot_error));
    CHECK(snapshot_error=="FOCUS_EVALUATION_RELATION_ENDPOINT_MISSING");
  }

  // Exercise the complete Focus Snapshot lifecycle against a live graph:
  // project a spatially focused root, capture a graph-bound manifest, then
  // mutate only the manifest's projection IDs while preserving its digest.
  {
    ContextGraph live_graph((root/"focus-live-manifest-validation.sgj").string());
    CHECK(live_graph.open());
    SemanticDelta seed;
    seed.delta_id="focus-live-seed";
    seed.idempotency_key=seed.delta_id;
    seed.expected_state_version=live_graph.state_version();
    seed.nodes={
      {{"live-root",NodeKind::Cluster,"live focus root",{}}},
      {{"live-member",NodeKind::Evidence,"live evidence member",{}}}
    };
    seed.relations={{{"live-membership",RelationKind::Contains,
        "live-root","live-member",1.0}}};
    std::string live_error;
    CHECK(live_graph.append_focus_region_update(seed,
        FocusRegion{{0.0,0.0,0.0},{1.0,1.0,1.0}},live_error));
    CHECK(live_graph.append_cluster_spatial_position_update(seed,
        ClusterSpatialPosition{"live-root",{0.0,0.0,0.0}},live_error));
    CHECK(live_graph.apply(seed).committed);

    const auto live_observation=live_graph.observe_focused_cognition(
        FocusRegion{{0.0,0.0,0.0},{1.0,1.0,1.0}},
        {ClusterSpatialPosition{"live-root",{0.0,0.0,0.0}}}, {}, 32,64,0);
    CHECK(live_observation.complete);
    CHECK(live_observation.logical_region_cluster_ids==
          std::vector<std::string>{"live-root"});
    CHECK(live_observation.processed_region_cluster_ids==
          std::vector<std::string>{"live-root"});
    SnapshotDocument live_manifest;
    CHECK(make_completed_focus_snapshot_document(live_graph,live_observation,
        "live-focus-digest","live-observation-digest","live-context",1,
        &live_manifest,live_error));
    CHECK(validate_focus_snapshot_against_graph(
        live_manifest,live_graph,live_error));

    std::string focus_value, graph_value, observation_value;
    std::vector<std::string> roots_value, nodes_value, relations_value;
    CHECK(focus_snapshot_detail::decode_manifest(live_manifest,focus_value,
        graph_value,observation_value,roots_value,nodes_value,relations_value));

    auto unknown_node=live_manifest;
    unknown_node.payload="FES1";
    focus_snapshot_detail::append_string(unknown_node.payload,focus_value);
    focus_snapshot_detail::append_string(unknown_node.payload,graph_value);
    focus_snapshot_detail::append_string(unknown_node.payload,observation_value);
    focus_snapshot_detail::append_set(unknown_node.payload,roots_value);
    nodes_value.push_back("ghost-node");
    focus_snapshot_detail::append_set(unknown_node.payload,nodes_value);
    focus_snapshot_detail::append_set(unknown_node.payload,relations_value);
    CHECK(!validate_focus_snapshot_against_graph(
        unknown_node,live_graph,live_error));
    CHECK(live_error=="FOCUS_SNAPSHOT_GRAPH_NODE_MISSING");

    nodes_value.erase(std::remove(nodes_value.begin(),nodes_value.end(),
        "ghost-node"),nodes_value.end());
    auto unknown_relation=live_manifest;
    unknown_relation.payload="FES1";
    focus_snapshot_detail::append_string(unknown_relation.payload,focus_value);
    focus_snapshot_detail::append_string(unknown_relation.payload,graph_value);
    focus_snapshot_detail::append_string(unknown_relation.payload,observation_value);
    focus_snapshot_detail::append_set(unknown_relation.payload,roots_value);
    focus_snapshot_detail::append_set(unknown_relation.payload,nodes_value);
    focus_snapshot_detail::append_set(unknown_relation.payload,{"ghost-relation"});
    CHECK(!validate_focus_snapshot_against_graph(
        unknown_relation,live_graph,live_error));
    CHECK(live_error=="FOCUS_SNAPSHOT_GRAPH_RELATION_MISSING");

    auto missing_endpoint=live_manifest;
    missing_endpoint.payload="FES1";
    focus_snapshot_detail::append_string(missing_endpoint.payload,focus_value);
    focus_snapshot_detail::append_string(missing_endpoint.payload,graph_value);
    focus_snapshot_detail::append_string(missing_endpoint.payload,observation_value);
    focus_snapshot_detail::append_set(missing_endpoint.payload,roots_value);
    focus_snapshot_detail::append_set(missing_endpoint.payload,{"live-root"});
    focus_snapshot_detail::append_set(missing_endpoint.payload,relations_value);
    CHECK(!validate_focus_snapshot_against_graph(
        missing_endpoint,live_graph,live_error));
    CHECK(live_error=="FOCUS_SNAPSHOT_GRAPH_RELATION_ENDPOINT_MISSING");

    auto invalid_root=live_manifest;
    invalid_root.payload="FES1";
    focus_snapshot_detail::append_string(invalid_root.payload,focus_value);
    focus_snapshot_detail::append_string(invalid_root.payload,graph_value);
    focus_snapshot_detail::append_string(invalid_root.payload,observation_value);
    focus_snapshot_detail::append_set(invalid_root.payload,{"live-member"});
    focus_snapshot_detail::append_set(invalid_root.payload,nodes_value);
    focus_snapshot_detail::append_set(invalid_root.payload,relations_value);
    CHECK(!validate_focus_snapshot_against_graph(
        invalid_root,live_graph,live_error));
    CHECK(live_error=="FOCUS_SNAPSHOT_GRAPH_ROOT_INVALID");
  }

  std::filesystem::remove_all(root);
  std::cout<<"SURGE ContextGraph tests: retention event ownership passed\n";
}
