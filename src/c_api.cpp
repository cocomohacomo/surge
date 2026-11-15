#include "surge/c_api.h"
#include "surge/context_graph.hpp"
#include "surge/event_journal.hpp"
#include "surge/packet_admission.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

namespace {
void copy_text(char* out, size_t capacity, const std::string& value) {
  if (!out || capacity == 0) return;
  const auto count = std::min(capacity - 1, value.size());
  std::memcpy(out, value.data(), count);
  out[count] = '\0';
}
bool node_kind(const char* value, surge::NodeKind& out) {
  if (!value) return false;
  const std::string s(value);
  if(s=="CLUSTER")out=surge::NodeKind::Cluster;
  else if(s=="ENTITY")out=surge::NodeKind::Entity;
  else if(s=="EVIDENCE")out=surge::NodeKind::Evidence;
  else if(s=="CLAIM")out=surge::NodeKind::Claim;
  else if(s=="QUESTION")out=surge::NodeKind::Question;
  else if(s=="QUESTION_ASPECT")out=surge::NodeKind::QuestionAspect;
  else if(s=="DEPENDENCY")out=surge::NodeKind::Dependency;
  else if(s=="INTEREST")out=surge::NodeKind::Interest;
  else return false;
  return true;
}
bool relation_kind(const char* value, surge::RelationKind& out) {
  if (!value) return false;
  const std::string s(value);
  if(s=="SUPPORTS")out=surge::RelationKind::Supports;
  else if(s=="CONTRADICTS")out=surge::RelationKind::Contradicts;
  else if(s=="AGREES_WITH")out=surge::RelationKind::AgreesWith;
  else if(s=="CONFLICTS_WITH")out=surge::RelationKind::ConflictsWith;
  else if(s=="DEPENDS_ON")out=surge::RelationKind::DependsOn;
  else if(s=="ABOUT")out=surge::RelationKind::About;
  else return false;
  return true;
}
struct Handle {
  surge::EventJournal journal;
  surge::PacketAdmissionCoordinator coordinator;
  surge::ContextGraph graph;
  explicit Handle(const std::string& path)
      : journal(path), coordinator(journal), graph(journal) {}
};
}
extern "C" {
void* surge_packet_core_open(const char* path, char* error, size_t cap) {
  try {
    if (!path || !*path) { copy_text(error, cap, "JOURNAL_PATH_REQUIRED"); return nullptr; }
    auto handle = std::make_unique<Handle>(path);
    if (!handle->journal.open()) { copy_text(error, cap, handle->journal.last_error()); return nullptr; }
    if (!handle->coordinator.recover()) { copy_text(error, cap, "PACKET_COORDINATOR_RECOVERY_FAILED"); return nullptr; }
    if (!handle->graph.open()) { copy_text(error, cap, "CONTEXT_GRAPH_RECOVERY_FAILED"); return nullptr; }
    copy_text(error, cap, "");
    return handle.release();
  } catch (const std::exception& ex) { copy_text(error, cap, ex.what()); return nullptr; }
    catch (...) { copy_text(error, cap, "UNKNOWN_CORE_EXCEPTION"); return nullptr; }
}
void surge_packet_core_close(void* opaque) { try { delete static_cast<Handle*>(opaque); } catch (...) {} }
int surge_packet_core_admit(
    void* opaque, const char* packet_id, const char* idempotency_key,
    const char* context_id, const char* source_id, const char* source_generation,
    const char* block_ref, const char* offset_basis, uint64_t start_offset,
    uint64_t end_offset, uint64_t source_sequence, const char* snapshot_digest,
    const char* range_digest, const char* request_digest,
    const char* revision_of_packet_id, int revision, uint64_t revision_sequence,
    const unsigned char* snapshot, size_t snapshot_size, int* duplicate,
    uint64_t* state_version, uint64_t* watermark, char* status, size_t status_cap,
    char* error, size_t error_cap) {
  try {
    if (!opaque || !packet_id || !idempotency_key || !context_id || !source_id ||
        !source_generation || !block_ref || !offset_basis || !snapshot_digest ||
        !range_digest || !request_digest || (!snapshot && snapshot_size != 0) ||
        (revision != 0 && revision != 1)) {
      copy_text(error, error_cap, "C_API_ARGUMENT_INVALID"); return 0;
    }
    surge::ValidatedRecognitionPacket packet;
    packet.packet_id=packet_id; packet.idempotency_key=idempotency_key;
    packet.context_id=context_id; packet.source_id=source_id;
    packet.source_generation=source_generation; packet.block_ref=block_ref;
    packet.offset_basis=offset_basis; packet.start_offset=start_offset;
    packet.end_offset=end_offset; packet.source_sequence=source_sequence;
    packet.source_snapshot_digest=snapshot_digest; packet.source_range_digest=range_digest;
    packet.canonical_request_digest=request_digest;
    packet.revision_of_packet_id=revision_of_packet_id ? revision_of_packet_id : "";
    packet.revision=revision==1; packet.revision_sequence=revision_sequence;
    const std::string source=snapshot_size==0 ? std::string() :
      std::string(reinterpret_cast<const char*>(snapshot),snapshot_size);
    const auto result=static_cast<Handle*>(opaque)->coordinator.admit(packet,source);
    if(duplicate)*duplicate=result.duplicate?1:0;
    if(state_version)*state_version=result.state_version;
    if(watermark)*watermark=result.watermark;
    copy_text(status,status_cap,result.packet_status);copy_text(error,error_cap,result.error);
    return result.admitted?1:0;
  } catch(const std::exception& ex){copy_text(error,error_cap,ex.what());return 0;}
    catch(...){copy_text(error,error_cap,"UNKNOWN_CORE_EXCEPTION");return 0;}
}
int surge_context_graph_apply(
    void* opaque,const char* delta_id,const char* idempotency_key,uint64_t expected,
    const SurgeGraphNodeInput* nodes,size_t node_count,
    const SurgeGraphRelationInput* relations,size_t relation_count,
    const char* focus_cluster_id,const char* focus_interest_id,
    int* duplicate,uint64_t* state_version,char* error,size_t error_cap) {
  try {
    if(!opaque||!delta_id||!idempotency_key||(node_count&&!nodes)||(relation_count&&!relations)||
       node_count>4096||relation_count>8192) {
      copy_text(error,error_cap,"C_API_SEMANTIC_ARGUMENT_INVALID");return 0;
    }
    surge::SemanticDelta delta;
    delta.delta_id=delta_id;delta.idempotency_key=idempotency_key;
    delta.expected_state_version=expected;
    for(size_t i=0;i<node_count;++i){
      if(!nodes[i].id||!nodes[i].kind||!nodes[i].content||
         (nodes[i].reference_count&&!nodes[i].references)||nodes[i].reference_count>1024){
        copy_text(error,error_cap,"C_API_NODE_ARGUMENT_INVALID");return 0;
      }
      surge::GraphNode node;node.id=nodes[i].id;node.content=nodes[i].content;
      if(!node_kind(nodes[i].kind,node.kind)){copy_text(error,error_cap,"C_API_NODE_KIND_INVALID");return 0;}
      for(size_t j=0;j<nodes[i].reference_count;++j){
        if(!nodes[i].references[j]){copy_text(error,error_cap,"C_API_NODE_REFERENCE_INVALID");return 0;}
        node.references.emplace_back(nodes[i].references[j]);
      }
      delta.nodes.push_back({std::move(node)});
    }
    for(size_t i=0;i<relation_count;++i){
      if(!relations[i].id||!relations[i].kind||!relations[i].from_id||!relations[i].to_id){
        copy_text(error,error_cap,"C_API_RELATION_ARGUMENT_INVALID");return 0;
      }
      surge::GraphRelation relation;relation.id=relations[i].id;
      relation.from=relations[i].from_id;relation.to=relations[i].to_id;
      if(!relation_kind(relations[i].kind,relation.kind)){copy_text(error,error_cap,"C_API_RELATION_KIND_INVALID");return 0;}
      delta.relations.push_back({std::move(relation)});
    }
    if(focus_cluster_id&&*focus_cluster_id)
      delta.focus=surge::SetFocus{{focus_cluster_id,focus_interest_id?focus_interest_id:""}};
    const auto result=static_cast<Handle*>(opaque)->graph.apply(delta);
    if(duplicate)*duplicate=result.duplicate?1:0;
    if(state_version)*state_version=result.state_version;
    copy_text(error,error_cap,result.error);
    return result.committed?1:0;
  } catch(const std::exception& ex){copy_text(error,error_cap,ex.what());return 0;}
    catch(...){copy_text(error,error_cap,"UNKNOWN_SEMANTIC_EXCEPTION");return 0;}
}
uint64_t surge_packet_core_state_version(void* opaque) {
  return opaque?static_cast<Handle*>(opaque)->coordinator.state_version():0;
}
size_t surge_packet_core_packet_count(void* opaque) {
  return opaque?static_cast<Handle*>(opaque)->coordinator.packet_count():0;
}
}
