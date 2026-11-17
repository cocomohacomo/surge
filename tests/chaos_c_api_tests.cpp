#include "surge/c_api.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#define CHECK(x) do{if(!(x))throw std::runtime_error("CHECK failed: " #x);}while(0)
int main(){
 auto root=std::filesystem::temp_directory_path()/"surge-chaos-c-api-tests";
 std::filesystem::remove_all(root);std::filesystem::create_directories(root);
 auto path=(root/"core.sgj").string();char error[512]{};uint64_t version=0;
 void* h=surge_packet_core_open(path.c_str(),error,sizeof(error));CHECK(h!=nullptr);
 SurgeChaosProvenanceInput p{"c-api-test","1","policy-1","2026-11-17T00:00:00Z"};
 CHECK(surge_chaos_cluster_create(h,"cc-create","cc-create-key",0,"chaos-c-api",&p,&version,error,sizeof(error))==1);CHECK(version==1);
 SurgeChaosMemberInput m1{"m1","event-1","NO_STABLE_TOPIC","2026-11-17T00:00:01Z",nullptr};
 SurgeChaosMemberInput m2{"m2","event-2","CREATIVE_EXPLORATION","2026-11-17T00:00:02Z",nullptr};
 CHECK(surge_chaos_cluster_admit_member(h,"cc-m1","cc-m1-key",1,"chaos-c-api",&m1,&p,&version,error,sizeof(error))==1);
 CHECK(surge_chaos_cluster_admit_member(h,"cc-m2","cc-m2-key",2,"chaos-c-api",&m2,&p,&version,error,sizeof(error))==1);
 SurgeChaosAssociationInput a{"analogy-1","m1","m2","ANALOGY"};
 CHECK(surge_chaos_cluster_propose_association(h,"cc-a1","cc-a1-key",3,"chaos-c-api",&a,&p,&version,error,sizeof(error))==1);
 CHECK(surge_chaos_cluster_transition(h,"cc-review","cc-review-key",4,"chaos-c-api","REVIEWABLE","candidate pattern",&p,&version,error,sizeof(error))==1);
 const char* refs[]={"m1"};const char* targets[]={"topic-1"};
 CHECK(surge_chaos_cluster_promote_subset(h,"cc-promote","cc-promote-key",5,"chaos-c-api",refs,targets,1,&p,&version,error,sizeof(error))==1);
 char output[4096]{};size_t required=0;
 CHECK(surge_chaos_cluster_get(h,"chaos-c-api",output,sizeof(output),&required,error,sizeof(error))==1);
 CHECK(std::string(output).find("NO_STABLE_TOPIC")!=std::string::npos);
 CHECK(std::string(output).find("topic-1")!=std::string::npos);
 CHECK(std::string(output).find("event-2")!=std::string::npos);
 surge_packet_core_close(h);
 h=surge_packet_core_open(path.c_str(),error,sizeof(error));CHECK(h!=nullptr);
 CHECK(surge_chaos_cluster_get(h,"chaos-c-api",output,sizeof(output),&required,error,sizeof(error))==1);
 CHECK(std::string(output).find("topic-1")!=std::string::npos);
 surge_packet_core_close(h);std::filesystem::remove_all(root);
 std::cout<<"SURGE Chaos Cluster C ABI: persistence and subset promotion passed\n";
}
