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
 EvidenceProvenanceSnapshotDomain ed(evidence);ChaosClusterSnapshotDomain cd(chaos);
 TemporalBoundarySnapshotDomain bd(boundary);TemporalQuestionSnapshotDomain qd(questions);SnapshotCoordinator co;
 CHECK(co.register_domain(&ed,&error));CHECK(co.register_domain(&cd,&error));CHECK(co.register_domain(&bd,&error));CHECK(co.register_domain(&qd,&error));
 std::string bytes;CHECK(co.capture("ctx",journal.state_version(),journal.state_version(),&bytes,&error));
 ep.revision=2;ep.observed_at="2026-11-19T00:01:00Z";CHECK(evidence.append(ep,journal.state_version(),er,error));
 CHECK(chaos.create("tx-create-b","idem-create-b",journal.state_version(),"chaos-b",cp).committed);
 auto b3=boundary.add("btx3","bidem3","b-event-3",journal.state_version(),{"claim/A","VALID_FROM","2026-06-01T00:00:00+09:00","","evidence/b3","packet/b3"});CHECK(b3.committed);
 auto q2=questions.synchronize("qtx2","qidem2","q-event-2",journal.state_version(),b3.pending);CHECK(q2.committed&&q2.status=="REOPENED");
 CHECK(evidence.history().at("evidence-a").size()==2);CHECK(chaos.list().size()==2);CHECK(boundary.pending_claim_count()==1);CHECK(questions.size()==1);
 CHECK(co.restore(bytes,"ctx",0,0,&error));
 CHECK(evidence.history().at("evidence-a").size()==1);CHECK(chaos.list().size()==1);CHECK(chaos.list().front().cluster_id=="chaos-a");
 TemporalBoundaryPending restored_boundary;CHECK(boundary.get("claim/A",restored_boundary));CHECK(restored_boundary.candidates.size()==2);
 TemporalQuestion restored_question;CHECK(questions.get("claim/A","VALID_FROM",restored_question));CHECK(restored_question.status=="OPEN"&&restored_question.candidate_ids.size()==2);
 std::string bad="EPH1 1 \"evidence-a\" 1 \"evidence-a\" 9 1 0 0 0 ";CHECK(!ed.prepare_restore(bad,&error));
 CHECK(evidence.history().at("evidence-a").size()==1);
 std::filesystem::remove(path,ec);
 std::cout<<"SURGE snapshot domain adapter tests passed\n";return 0;}
