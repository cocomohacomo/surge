#include "surge/snapshot_domains.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<"CHECK failed: "#x<<" at "<<__LINE__<<"\n";std::exit(1);}}while(0)
int main(){using namespace surge;
 const auto path=(std::filesystem::temp_directory_path()/"surge_snapshot_domain_adapters.journal").string();std::error_code ec;std::filesystem::remove(path,ec);
 EventJournal journal(path);CHECK(journal.open());
 EvidenceProvenanceLedger evidence(journal);CHECK(evidence.replay());
 ChaosClusterLedger chaos(journal);CHECK(chaos.open());
 TemporalBoundaryLedger boundary(journal);CHECK(boundary.replay());
 TemporalQuestionLedger questions(journal);CHECK(questions.replay());
 ChaosProvenance cp{"test","1","policy-1","2026-11-19T00:00:00Z"};
 CHECK(chaos.create("tx-create-a","idem-create-a",journal.state_version(),"chaos-a",cp).committed);
 EvidenceProvenance ep;ep.evidence_id="evidence-a";ep.revision=1;ep.observed_at="2026-11-19T00:00:00Z";EvidenceProvenanceRecord er;std::string error;
 CHECK(evidence.append(ep,journal.state_version(),er,error));
 auto b1=boundary.add("btx1","bidem1","b-event-1",journal.state_version(),{"claim/A","VALID_FROM","2026-04-01T00:00:00+09:00","","evidence/b1","packet/b1"});CHECK(b1.committed);
 auto b2=boundary.add("btx2","bidem2","b-event-2",journal.state_version(),{"claim/A","VALID_FROM","2026-05-01T00:00:00+09:00","","evidence/b2","packet/b2"});CHECK(b2.committed&&b2.status=="BOUNDARY_CONFLICT_REVIEW_REQUIRED");
 auto q1=questions.synchronize("qtx1","qidem1","q-event-1",journal.state_version(),b2.pending);CHECK(q1.committed&&q1.status=="OPEN");

 ContextGraph graph(journal);CHECK(graph.open());
 SemanticDelta seed;seed.delta_id="graph-seed";seed.idempotency_key="graph-seed";seed.expected_state_version=journal.state_version();
 seed.nodes={{{"cluster/G",NodeKind::Cluster,"approved",{}}},
   {{"cluster/H",NodeKind::Cluster,"target",{}}},
   {{"evidence/G",NodeKind::Evidence,"source evidence",{}}},
   {{"claim/G",NodeKind::Claim,"claim text",{"evidence/G"}}},
   {{"question/G",NodeKind::Question,"unresolved question",{}}},
   {{"aspect/G",NodeKind::QuestionAspect,"validity aspect",{"question/G"}}},
   {{"interest/G",NodeKind::Interest,"interest",{"aspect/G","cluster/G"}}},
   {{"dependency/G",NodeKind::Dependency,"CONTENT_EQUALS:approved",{"cluster/G","cluster/H"}}}};
 seed.relations={{{"supports/G",RelationKind::Supports,"evidence/G","claim/G"}}};
 seed.focus=SetFocus{{"cluster/G","interest/G",false}};
 seed.claim_validity_updates={{"claim/G","2026-01-01T00:00:00Z","2027-01-01T00:00:00Z","evidence/G",1,"",""}};
 seed.evidence_validity_updates={{"evidence/G","2026-01-01T00:00:00Z","2027-01-01T00:00:00Z","evidence/G",1,"",""}};
 CHECK(graph.apply(seed).committed);
 CHECK(graph.dependency_evaluations().count("graph-seed")==1);
 CHECK(graph.latest_claim_validity("claim/G").has_value());
 CHECK(graph.latest_evidence_validity("evidence/G").has_value());
 CHECK(graph.set_question_aspect_status("aspect/G",QuestionAspectStatus::Resolved,graph.state_version(),error));
 bool reactivated=false;CHECK(graph.record_question_reference("question/G",graph.state_version(),reactivated,error));
 CHECK(graph.set_question_memory_state("question/G",QuestionMemoryState::Dormant,graph.state_version(),error));
 TemporalReassessmentRecord reassessment;
 CHECK(graph.reassess_claim("claim/G","2026-11-19T00:00:00Z",graph.state_version(),reassessment,error));

 EvidenceProvenanceSnapshotDomain ed(evidence);ChaosClusterSnapshotDomain cd(chaos);
 TemporalBoundarySnapshotDomain bd(boundary);TemporalQuestionSnapshotDomain qd(questions);
 ContextGraphSnapshotDomain gd(graph);SnapshotCoordinator co;
 CHECK(co.register_domain(&ed,&error));CHECK(co.register_domain(&cd,&error));CHECK(co.register_domain(&bd,&error));CHECK(co.register_domain(&qd,&error));CHECK(co.register_domain(&gd,&error));
 std::string bytes;CHECK(co.capture("ctx",journal.state_version(),journal.state_version(),&bytes,&error));

 ep.revision=2;ep.observed_at="2026-11-19T00:01:00Z";CHECK(evidence.append(ep,journal.state_version(),er,error));
 CHECK(chaos.create("tx-create-b","idem-create-b",journal.state_version(),"chaos-b",cp).committed);
 auto b3=boundary.add("btx3","bidem3","b-event-3",journal.state_version(),{"claim/A","VALID_FROM","2026-06-01T00:00:00+09:00","","evidence/b3","packet/b3"});CHECK(b3.committed);
 auto q2=questions.synchronize("qtx2","qidem2","q-event-2",journal.state_version(),b3.pending);CHECK(q2.committed&&q2.status=="UNDER_REVIEW");
 SemanticDelta extra;extra.delta_id="graph-extra";extra.idempotency_key="graph-extra";extra.expected_state_version=journal.state_version();extra.nodes={{{"extra/G",NodeKind::Entity,"post-snapshot",{}}}};CHECK(graph.apply(extra).committed);
 CHECK(evidence.history().at("evidence-a").size()==2);CHECK(chaos.list().size()==2);CHECK(boundary.pending_claim_count()==1);CHECK(questions.size()==1);CHECK(graph.nodes().count("extra/G")==1);

 CHECK(co.restore(bytes,"ctx",0,0,&error));
 CHECK(evidence.history().at("evidence-a").size()==1);CHECK(chaos.list().size()==1);CHECK(chaos.list().front().cluster_id=="chaos-a");
 TemporalBoundaryPending restored_boundary;CHECK(boundary.get("claim/A",restored_boundary));CHECK(restored_boundary.candidates.size()==2);
 TemporalQuestion restored_question;CHECK(questions.get("claim/A","VALID_FROM",restored_question));CHECK(restored_question.status=="OPEN"&&restored_question.candidate_ids.size()==2);
 CHECK(graph.nodes().size()==8);CHECK(graph.nodes().count("extra/G")==0);CHECK(graph.relations().size()==1);
 CHECK(graph.focus().has_value()&&graph.focus()->cluster_id=="cluster/G");CHECK(graph.focus_history().size()==1);
 CHECK(graph.question_aspect_status("aspect/G")==QuestionAspectStatus::Resolved);
 CHECK(graph.question_memory_state("question/G")==QuestionMemoryState::Dormant);
 CHECK(graph.question_reference_count("question/G")==1);
 CHECK(graph.latest_claim_validity("claim/G")->validity.revision==1);
 CHECK(graph.latest_evidence_validity("evidence/G")->validity.revision==1);
 CHECK(graph.dependency_evaluations().count("graph-seed")==1);
 CHECK(graph.reassessment_history().at("claim/G").size()==1);

 std::string bad="EPH1 1 \"evidence-a\" 1 \"evidence-a\" 9 1 0 0 0 ";CHECK(!ed.prepare_restore(bad,&error));
 CHECK(evidence.history().at("evidence-a").size()==1);
 std::filesystem::remove(path,ec);
 std::cout<<"SURGE snapshot domain adapter tests passed\n";return 0;}
