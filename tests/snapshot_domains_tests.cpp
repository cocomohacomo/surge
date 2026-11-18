#include "surge/snapshot_domains.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<"CHECK failed: "#x<<" at "<<__LINE__<<"\n";std::exit(1);}}while(0)
int main(){using namespace surge;
 const auto path=(std::filesystem::temp_directory_path()/"surge_snapshot_domain_adapters.journal").string();std::error_code ec;std::filesystem::remove(path,ec);
 EventJournal journal(path);CHECK(journal.open());EvidenceProvenanceLedger evidence(journal);CHECK(evidence.replay());ChaosClusterLedger chaos(journal);CHECK(chaos.open());
 ChaosProvenance cp{"test","1","policy-1","2026-11-19T00:00:00Z"};
 CHECK(chaos.create("tx-create-a","idem-create-a",journal.state_version(),"chaos-a",cp).committed);
 EvidenceProvenance ep;ep.evidence_id="evidence-a";ep.revision=1;ep.observed_at="2026-11-19T00:00:00Z";EvidenceProvenanceRecord er;std::string error;
 CHECK(evidence.append(ep,journal.state_version(),er,error));EvidenceProvenanceSnapshotDomain ed(evidence);ChaosClusterSnapshotDomain cd(chaos);SnapshotCoordinator co;
 CHECK(co.register_domain(&ed,&error));CHECK(co.register_domain(&cd,&error));std::string bytes;
 CHECK(co.capture("ctx",journal.state_version(),journal.state_version(),&bytes,&error));
 ep.revision=2;ep.observed_at="2026-11-19T00:01:00Z";CHECK(evidence.append(ep,journal.state_version(),er,error));
 CHECK(chaos.create("tx-create-b","idem-create-b",journal.state_version(),"chaos-b",cp).committed);
 CHECK(evidence.history().at("evidence-a").size()==2);CHECK(chaos.list().size()==2);
 CHECK(co.restore(bytes,"ctx",0,0,&error));CHECK(evidence.history().at("evidence-a").size()==1);CHECK(chaos.list().size()==1);CHECK(chaos.list().front().cluster_id=="chaos-a");
 std::string bad="EPH1 1 \"evidence-a\" 1 \"evidence-a\" 9 1 0 0 0 ";CHECK(!ed.prepare_restore(bad,&error));
 CHECK(evidence.history().at("evidence-a").size()==1);std::filesystem::remove(path,ec);
 std::cout<<"SURGE snapshot domain adapter tests passed\n";return 0;}
