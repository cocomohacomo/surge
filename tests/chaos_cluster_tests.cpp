#include "surge/chaos_cluster.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error("CHECK failed: " #x);}while(0)
int main(){
 auto root=std::filesystem::temp_directory_path()/"surge-chaos-ledger-tests";
 std::filesystem::remove_all(root);std::filesystem::create_directories(root);
 auto path=(root/"chaos.sgj").string();
 surge::ChaosProvenance p{"test","1","policy-1","2026-11-17T00:00:00Z"};
 {
  surge::ChaosClusterLedger l(path);CHECK(l.open());
  auto r=l.create("create","ik-create",0,"chaos-1",p);CHECK(r.committed&&r.state_version==1);
  CHECK(l.create("create-retry","ik-create-retry",1,"chaos-1",p).duplicate);
  surge::ChaosMember a{"m1","event-1","NO_STABLE_TOPIC","2026-11-17T00:00:01Z","",p};
  surge::ChaosMember b{"m2","event-2","CREATIVE_EXPLORATION","2026-11-17T00:00:02Z","",p};
  CHECK(l.admit_member("m1-op","ik-m1",1,"chaos-1",a).committed);
  CHECK(l.admit_member("m1-retry","ik-m1-retry",2,"chaos-1",a).duplicate);
  CHECK(l.admit_member("m2-op","ik-m2",2,"chaos-1",b).committed);
  // CC12: the same operation/idempotency identity cannot carry changed content.
  surge::ChaosMember altered{"m1-altered","event-altered","NO_STABLE_TOPIC","2026-11-17T00:00:05Z","",p};
  auto reused_identity=l.admit_member("m1-op","ik-m1",l.state_version(),"chaos-1",altered);
  CHECK(!reused_identity.committed&&reused_identity.error=="IDEMPOTENCY_KEY_REUSED");
  surge::ChaosAssociation x{"a1","m1","m2",surge::ChaosAssociationType::Analogy,surge::ChaosAssociationStatus::Reviewed,p};
  CHECK(l.propose_association("a1-op","ik-a1",3,"chaos-1",x).committed);
  surge::ChaosCluster c;CHECK(l.get("chaos-1",c));
  CHECK(c.associations.at("a1").status==surge::ChaosAssociationStatus::Provisional);
  CHECK(c.associations.at("a1").history.size()==1);
  CHECK(l.review_association("review-a1","ik-review-a1",4,"chaos-1","a1",surge::ChaosAssociationStatus::Rejected,p).committed);
  CHECK(l.get("chaos-1",c));CHECK(c.associations.at("a1").history.size()==2);
  auto terminal=l.review_association("review-again","ik-review-again",5,"chaos-1","a1",surge::ChaosAssociationStatus::Reviewed,p);
  CHECK(!terminal.committed&&terminal.error=="CHAOS_ASSOCIATION_REVIEW_TERMINAL");
  surge::ChaosPatternHypothesis h;h.hypothesis_id="hyp-1";h.statement="two fragments may share a latent pattern";
  h.member_refs={"m1","m2"};h.counterexample_refs={"m2"};h.provenance=p;
  CHECK(l.add_pattern_hypothesis("hyp-op","ik-hyp",5,"chaos-1",h).committed);
  CHECK(l.transition("review","ik-review",6,"chaos-1",surge::ChaosLifecycle::Reviewable,"candidate ready",p).committed);
  CHECK(l.promote_subset("promote","ik-promote",7,"chaos-1",{{"m1","topic-A"}},p).committed);
  CHECK(l.get("chaos-1",c));CHECK(c.state_version==8&&c.members.size()==2&&c.promoted_member_targets.size()==1&&c.members.count("m2"));
  CHECK(l.transition("reopen","ik-reopen",8,"chaos-1",surge::ChaosLifecycle::Accumulating,"continue exploration",p).committed);
  const auto stale_promotion_version=l.state_version();
  CHECK(l.transition("review2","ik-review2",9,"chaos-1",surge::ChaosLifecycle::Reviewable,"review residual",p).committed);
  // CC10: promotion from an older snapshot must not partially promote a subset.
  auto stale_promotion=l.promote_subset("stale-promote","ik-stale-promote",stale_promotion_version,
    "chaos-1",{{"m2","topic-B"}},p);
  CHECK(!stale_promotion.committed&&stale_promotion.error=="STATE_VERSION_CONFLICT");
  CHECK(l.get("chaos-1",c));CHECK(c.promoted_member_targets.count("m2")==0&&c.members.count("m2")==1);
  CHECK(l.fork_subset("fork","ik-fork",10,"chaos-1","chaos-child",{"m1","m2"},p).committed);
  CHECK(l.get("chaos-child",c));CHECK(c.parent_refs.size()==1&&c.parent_refs[0]=="chaos-1"&&c.members.size()==2);
  CHECK(l.get("chaos-1",c));CHECK(c.lifecycle==surge::ChaosLifecycle::Forked&&c.child_refs.size()==1);
  CHECK(l.create("create-2","ik-create-2",11,"chaos-2",p).committed);
  surge::ChaosMember c1{"x1","event-x1","NO_STABLE_TOPIC","2026-11-17T00:00:03Z","",p};
  surge::ChaosMember c2{"x2","event-x2","NO_STABLE_TOPIC","2026-11-17T00:00:04Z","",p};
  CHECK(l.admit_member("x1-op","ik-x1",12,"chaos-2",c1).committed);
  CHECK(l.admit_member("x2-op","ik-x2",13,"chaos-2",c2).committed);
  CHECK(l.transition("review-2","ik-review-2",14,"chaos-2",surge::ChaosLifecycle::Reviewable,"ready to partition",p).committed);
  CHECK(l.split_subsets("split","ik-split",15,"chaos-2",{{"chaos-2a",{"x1"}},{"chaos-2b",{"x2"}}},p).committed);
  surge::ChaosAssociation expiring{"a-expire","x1","x2",surge::ChaosAssociationType::CoOccurs,
    surge::ChaosAssociationStatus::Provisional,p};
  expiring.valid_from="2026-11-17T00:00:00Z";
  expiring.valid_until="2026-11-18T00:00:00Z";
  CHECK(l.propose_association("expire-propose","ik-expire-propose",l.state_version(),"chaos-2",expiring).committed);
  CHECK(l.expire_due_associations("expire-due","ik-expire-due",l.state_version(),"chaos-2",
    "2026-11-18T00:00:00Z",p).committed);
  CHECK(l.get("chaos-2",c));
  CHECK(c.associations.at("a-expire").status==surge::ChaosAssociationStatus::Expired);
  CHECK(c.associations.at("a-expire").history.size()==2);
  CHECK(l.get("chaos-2a",c));CHECK(c.members.size()==1&&c.parent_refs.size()==1);
  CHECK(l.get("chaos-2",c));CHECK(c.members.size()==2&&c.child_refs.size()==2&&c.lifecycle==surge::ChaosLifecycle::Forked);
  CHECK(l.create("score-create","ik-score-create",l.state_version(),"chaos-score",p).committed);
  surge::ChaosMember score_member{"score-member","score-event","HIGH_NOVELTY","2026-11-17T00:00:30Z","",p};
  CHECK(l.admit_member("score-member-add","ik-score-member-add",l.state_version(),"chaos-score",score_member).committed);
  // CC02/CC06: co-membership is not a semantic edge, and producer disagreement
  // remains visible as separate member provenance.
  surge::ChaosProvenance producer_b{"recognizer-B","2","policy-B","2026-11-17T00:04:30Z"};
  surge::ChaosMember score_member_b{"score-member-b","score-event-b","NO_STABLE_TOPIC",
    "2026-11-17T00:04:31Z","",producer_b};
  CHECK(l.admit_member("score-member-b-add","ik-score-member-b-add",l.state_version(),
    "chaos-score",score_member_b).committed);
  surge::ChaosCluster disagreement_snapshot;
  CHECK(l.get("chaos-score",disagreement_snapshot));
  CHECK(disagreement_snapshot.members.size()==2&&disagreement_snapshot.associations.empty());
  CHECK(disagreement_snapshot.members.at("score-member").provenance.producer_id=="test");
  CHECK(disagreement_snapshot.members.at("score-member-b").provenance.producer_id=="recognizer-B");
  // CC04: incompatible candidate patterns coexist, linked only by a provisional
  // CONTRADICTION association, without forcing either hypothesis to resolve.
  surge::ChaosAssociation contradiction{"score-contradiction","score-member","score-member-b",
    surge::ChaosAssociationType::Contradiction,surge::ChaosAssociationStatus::Provisional,p};
  CHECK(l.propose_association("score-contradiction-add","ik-score-contradiction-add",
    l.state_version(),"chaos-score",contradiction).committed);
  surge::ChaosPatternHypothesis candidate_a;
  candidate_a.hypothesis_id="candidate-a";candidate_a.statement="candidate pattern A";
  candidate_a.member_refs={"score-member"};candidate_a.provenance=p;
  surge::ChaosPatternHypothesis candidate_b;
  candidate_b.hypothesis_id="candidate-b";candidate_b.statement="incompatible candidate pattern B";
  candidate_b.member_refs={"score-member-b"};candidate_b.provenance=producer_b;
  CHECK(l.add_pattern_hypothesis("candidate-a-add","ik-candidate-a-add",
    l.state_version(),"chaos-score",candidate_a).committed);
  CHECK(l.add_pattern_hypothesis("candidate-b-add","ik-candidate-b-add",
    l.state_version(),"chaos-score",candidate_b).committed);
  CHECK(l.get("chaos-score",disagreement_snapshot));
  CHECK(disagreement_snapshot.pattern_hypotheses.count("candidate-a")==1);
  CHECK(disagreement_snapshot.pattern_hypotheses.count("candidate-b")==1);
  CHECK(disagreement_snapshot.associations.at("score-contradiction").status==
    surge::ChaosAssociationStatus::Provisional);
  CHECK(l.revise_membership("score-member-remove","ik-score-member-remove",l.state_version(),
    "chaos-score","score-member","PROVISIONAL",false,p).committed);
  CHECK(l.revise_membership("score-member-restore","ik-score-member-restore",l.state_version(),
    "chaos-score","score-member","REVIEWED",true,p).committed);
  surge::ChaosCluster membership_snapshot;CHECK(l.get("chaos-score",membership_snapshot));
  CHECK(!membership_snapshot.members.at("score-member").membership_history.empty());
  CHECK(membership_snapshot.members.at("score-member").membership_history.size()==3);
  CHECK(membership_snapshot.members.at("score-member").membership_active);
  CHECK(membership_snapshot.members.at("score-member").membership_status=="REVIEWED");
  surge::ChaosPatternHypothesis score_hypothesis;
  score_hypothesis.hypothesis_id="score-hypothesis";
  score_hypothesis.statement="provisional pattern";
  score_hypothesis.member_refs={"score-member"};
  score_hypothesis.excluded_member_refs={"score-member"};
  score_hypothesis.provenance=p;
  CHECK(l.add_pattern_hypothesis("score-hypothesis-add","ik-score-hypothesis-add",
    l.state_version(),"chaos-score",score_hypothesis).committed);
  CHECK(l.review_pattern_hypothesis("score-hypothesis-review","ik-score-hypothesis-review",
    l.state_version(),"chaos-score","score-hypothesis","REVIEWABLE","UNVERIFIED",
    {{"score-member","insufficient independent support"}},p).committed);
  surge::ChaosScoreObservation novelty{0.91,"recognizer","2.1","score-policy-1","2026-11-17T00:01:00Z"};
  auto score_version=l.state_version();
  CHECK(l.record_observation("score-novelty","ik-score-novelty",score_version,"chaos-score","novelty",novelty).committed);
  novelty.value=0.84;novelty.observed_at="2026-11-17T00:02:00Z";
  CHECK(l.record_observation("score-novelty-2","ik-score-novelty-2",l.state_version(),"chaos-score","novelty",novelty).committed);
  surge::ChaosScoreObservation entity_score{0.77,"entity-scorer","3.0","entity-policy","2026-11-17T00:04:00Z"};
  CHECK(l.record_entity_observation("member-salience","ik-member-salience",l.state_version(),
    "chaos-1","MEMBER","m1",entity_score).committed);
  CHECK(l.record_entity_observation("association-confidence","ik-association-confidence",l.state_version(),
    "chaos-1","ASSOCIATION","a1",entity_score).committed);
  CHECK(l.record_entity_observation("hypothesis-confidence","ik-hypothesis-confidence",l.state_version(),
    "chaos-1","HYPOTHESIS","hyp-1",entity_score).committed);
  auto invalid=novelty;invalid.value=1.01;
  auto bad_score=l.record_observation("score-invalid","ik-score-invalid",l.state_version(),"chaos-score","salience",invalid);
  CHECK(!bad_score.committed&&bad_score.error=="CHAOS_SCORE_OBSERVATION_INVALID");
  auto bad_dimension=l.record_observation("score-invalid-dim","ik-score-invalid-dim",l.state_version(),"chaos-score","truth",novelty);
  CHECK(!bad_dimension.committed&&bad_dimension.error=="CHAOS_SCORE_OBSERVATION_INVALID");
  CHECK(l.get("chaos-score",c));
  CHECK(c.observations.at("novelty").size()==2&&c.observations.at("novelty").front().value==0.91);
  CHECK(l.create("merge-target-create","ik-merge-target-create",l.state_version(),"merge-target",p).committed);
  CHECK(l.create("merge-source-create","ik-merge-source-create",l.state_version(),"merge-source",p).committed);
  surge::ChaosMember merge_member{"merge-member","merge-event","HIGH_NOVELTY","2026-11-17T00:05:00Z","",p};
  CHECK(l.admit_member("merge-member-add","ik-merge-member-add",l.state_version(),"merge-source",merge_member).committed);
  CHECK(l.transition("merge-source-review","ik-merge-source-review",l.state_version(),"merge-source",
    surge::ChaosLifecycle::Reviewable,"ready for merge",p).committed);
  CHECK(l.merge_clusters("merge","ik-merge",l.state_version(),"merge-target",{"merge-source"},p).committed);
  // CC19: archived clusters require an explicit, provenance-bearing reactivation.
  CHECK(l.transition("reactivate","ik-reactivate",l.state_version(),"merge-source",
    surge::ChaosLifecycle::Dormant,"new evidence warrants renewed attention",p).committed);
  surge::ChaosMember late_member{"late-member","late-event","NEW_EVIDENCE",
    "2026-11-17T00:06:00Z","",p};
  CHECK(l.admit_member("late-member-add","ik-late-member-add",l.state_version(),
    "merge-source",late_member).committed);
  CHECK(l.get("merge-target",c));
  CHECK(c.members.count("merge-member")==1&&c.parent_refs.size()==1&&c.parent_refs[0]=="merge-source");
  CHECK(l.get("merge-source",c));
  CHECK(c.lifecycle==surge::ChaosLifecycle::Archived&&c.child_refs.size()==1&&c.child_refs[0]=="merge-target");
 }
 {
  surge::ChaosClusterLedger l(path);CHECK(l.open());
  surge::ChaosCluster c;CHECK(l.get("chaos-1",c));
  CHECK(c.members.size()==2&&c.associations.size()==1&&c.associations.at("a1").history.size()==2);
  CHECK(c.pattern_hypotheses.count("hyp-1")==1&&c.promoted_member_targets.at("m1")=="topic-A");
  CHECK(l.get("chaos-child",c));CHECK(c.parent_refs.size()==1&&c.members.size()==2);
  CHECK(l.get("chaos-2b",c));CHECK(c.parent_refs.size()==1&&c.members.size()==1);
  CHECK(l.get("chaos-2",c));
  CHECK(c.associations.at("a-expire").valid_until=="2026-11-18T00:00:00Z");
  CHECK(c.associations.at("a-expire").status==surge::ChaosAssociationStatus::Expired);
  CHECK(l.get("chaos-score",c));CHECK(c.observations.at("novelty").size()==2);
  CHECK(c.observations.at("novelty")[0].scorer_id=="recognizer");
  CHECK(c.observations.at("novelty")[1].value==0.84);
  CHECK(c.lifecycle_history.size()==2);
  CHECK(c.lifecycle_history.front().lifecycle=="OPEN");
  CHECK(c.lifecycle_history.back().reason=="FIRST_MEMBER_ADMITTED");
  CHECK(c.members.at("score-member").membership_history.size()==3);
  CHECK(c.members.at("score-member").membership_history[1].active==false);
  CHECK(c.members.at("score-member").membership_history[2].active==true);
  CHECK(c.pattern_hypotheses.at("score-hypothesis").history.size()==2);
  CHECK(c.pattern_hypotheses.at("score-hypothesis").exclusion_reasons.at("score-member")=="insufficient independent support");
  CHECK(l.get("chaos-1",c));
  CHECK(c.members.at("m1").salience_observations.size()==1);
  CHECK(c.associations.at("a1").confidence_observations.size()==1);
  CHECK(c.pattern_hypotheses.at("hyp-1").confidence_observations.size()==1);
  const auto v=l.state_version();
  CHECK(l.get("merge-target",c));CHECK(c.members.count("merge-member")==1);
  CHECK(l.get("merge-source",c));CHECK(c.lifecycle==surge::ChaosLifecycle::Dormant);
  CHECK(c.members.count("merge-member")==1&&c.members.count("late-member")==1);
  auto stale=l.transition("stale","ik-stale",v-1,"chaos-1",surge::ChaosLifecycle::Dormant,"stale",p);
  CHECK(!stale.committed&&stale.error=="STATE_VERSION_CONFLICT");
  auto bad=l.transition("bad","ik-bad",v,"chaos-1",surge::ChaosLifecycle::Open,"backward",p);
  CHECK(!bad.committed&&bad.error=="CHAOS_LIFECYCLE_TRANSITION_INVALID");
 }
 std::filesystem::remove_all(root);
 std::cout<<"SURGE Chaos Cluster v1 Core: persistence, replay, history, hypotheses, subset promotion, fork/split passed\n";
}
