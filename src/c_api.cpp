#include "surge/c_api.h"
#include "surge/context_graph.hpp"
#include "surge/context_hint.hpp"
#include "surge/event_journal.hpp"
#include "surge/packet_admission.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <sstream>
#include <set>
#include <vector>
#include <cctype>

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
bool scope_kind(const char* value, surge::ScopeKind& out) {
  if (!value) return false;
  const std::string s(value);
  if(s=="NEGATION")out=surge::ScopeKind::Negation;
  else if(s=="CONDITION")out=surge::ScopeKind::Condition;
  else if(s=="MODALITY")out=surge::ScopeKind::Modality;
  else if(s=="CONCESSION")out=surge::ScopeKind::Concession;
  else if(s=="CONTRAST")out=surge::ScopeKind::Contrast;
  else if(s=="COREFERENCE")out=surge::ScopeKind::Coreference;
  else if(s=="ANAPHORA")out=surge::ScopeKind::Anaphora;
  else if(s=="QUOTATION")out=surge::ScopeKind::Quotation;
  else if(s=="TEMPORAL")out=surge::ScopeKind::Temporal;
  else if(s=="LIST_CONTINUATION")out=surge::ScopeKind::ListContinuation;
  else if(s=="TABLE_HEADER")out=surge::ScopeKind::TableHeader;
  else return false;
  return true;
}
surge::ScopeAnchor scope_anchor(const SurgeScopeAnchorInput& in) {
  surge::ScopeAnchor a;
  a.anchor_id=in.anchor_id?in.anchor_id:"";
  a.source_digest=in.source_digest?in.source_digest:"";
  a.offset_basis=in.offset_basis?in.offset_basis:"";
  a.start_offset=static_cast<size_t>(in.start_offset);
  a.end_offset=static_cast<size_t>(in.end_offset);
  a.exact_text=in.exact_text?in.exact_text:"";
  a.packet_ref=in.packet_ref?in.packet_ref:"";
  return a;
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
  surge::PersistentRetrievalTicketRegistry tickets;
  explicit Handle(const std::string& path)
      : journal(path), coordinator(journal), graph(journal), tickets(journal) {}
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
    if (!handle->tickets.open()) { copy_text(error, cap, "RETRIEVAL_TICKET_RECOVERY_FAILED"); return nullptr; }
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

int surge_context_graph_apply_scoped(
    void* opaque,const char* delta_id,const char* idempotency_key,uint64_t expected,
    const SurgeGraphNodeInput* nodes,size_t node_count,
    const SurgeGraphRelationInput* relations,size_t relation_count,
    const SurgeScopeSpanInput* scopes,size_t scope_count,
    const char* source_snapshot,size_t source_snapshot_size,
    const char* focus_cluster_id,const char* focus_interest_id,
    int* duplicate,uint64_t* state_version,char* error,size_t error_cap) {
  try {
    if(!opaque||!delta_id||!idempotency_key||(node_count&&!nodes)||
       (relation_count&&!relations)||(scope_count&&!scopes)||
       node_count>4096||relation_count>8192||scope_count>4096||
       (!source_snapshot&&source_snapshot_size!=0)) {
      copy_text(error,error_cap,"C_API_SCOPED_ARGUMENT_INVALID");return 0;
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
    const std::string source=source_snapshot_size?std::string(source_snapshot,source_snapshot_size):std::string();
    for(size_t i=0;i<scope_count;++i){
      const auto& input=scopes[i];
      if(!input.span_id||!input.kind||!input.source_digest||!input.offset_basis||
         (input.has_closer!=0&&input.has_closer!=1)||
         (input.target_count&&!input.target_refs)||(input.antecedent_count&&!input.antecedent_refs)||
         (input.packet_count&&!input.packet_refs)||input.target_count>1024||
         input.antecedent_count>1024||input.packet_count>4096){
        copy_text(error,error_cap,"C_API_SCOPE_ARGUMENT_INVALID");return 0;
      }
      surge::ScopeSpanProposal span;span.span_id=input.span_id;
      if(!scope_kind(input.kind,span.kind)){copy_text(error,error_cap,"C_API_SCOPE_KIND_INVALID");return 0;}
      span.source_digest=input.source_digest;span.offset_basis=input.offset_basis;
      span.opener=scope_anchor(input.opener);
      if(input.has_closer)span.closer=scope_anchor(input.closer);
      for(size_t j=0;j<input.target_count;++j){if(!input.target_refs[j]){copy_text(error,error_cap,"C_API_SCOPE_TARGET_INVALID");return 0;}span.target_refs.emplace_back(input.target_refs[j]);}
      for(size_t j=0;j<input.antecedent_count;++j){if(!input.antecedent_refs[j]){copy_text(error,error_cap,"C_API_SCOPE_ANTECEDENT_INVALID");return 0;}span.antecedent_refs.emplace_back(input.antecedent_refs[j]);}
      for(size_t j=0;j<input.packet_count;++j){if(!input.packet_refs[j]){copy_text(error,error_cap,"C_API_SCOPE_PACKET_INVALID");return 0;}span.packet_refs.emplace_back(input.packet_refs[j]);}
      span.attribution_ref=input.attribution_ref?input.attribution_ref:"";
      span.modality_kind=input.modality_kind?input.modality_kind:"";
      span.temporal_role=input.temporal_role?input.temporal_role:"";
      span.parent_ref=input.parent_ref?input.parent_ref:"";
      delta.scope_spans.push_back({std::move(span),source});
    }
    if(focus_cluster_id&&*focus_cluster_id)
      delta.focus=surge::SetFocus{{focus_cluster_id,focus_interest_id?focus_interest_id:""}};
    const auto result=static_cast<Handle*>(opaque)->graph.apply(delta);
    if(duplicate)*duplicate=result.duplicate?1:0;
    if(state_version)*state_version=result.state_version;
    copy_text(error,error_cap,result.error);
    return result.committed?1:0;
  } catch(const std::exception& ex){copy_text(error,error_cap,ex.what());return 0;}
    catch(...){copy_text(error,error_cap,"UNKNOWN_SCOPED_SEMANTIC_EXCEPTION");return 0;}
}


int surge_retrieval_ticket_issue(void* opaque, const SurgeRetrievalTicketInput* input,
    char* error, size_t error_cap) {
  try {
    if (!opaque || !input || !input->ticket_id || !input->context_id ||
        !input->packet_ref || !input->source_range_digest || !input->omitted_digest ||
        !input->policy_version || (input->omitted_count && !input->omitted_refs) ||
        input->omitted_count > 100000) {
      copy_text(error, error_cap, "RETRIEVAL_TICKET_ARGUMENT_INVALID"); return 0;
    }
    surge::RetrievalTicket ticket;
    ticket.ticket_id=input->ticket_id; ticket.context_id=input->context_id;
    ticket.packet_ref=input->packet_ref; ticket.source_range_digest=input->source_range_digest;
    ticket.omitted_digest=input->omitted_digest; ticket.policy_version=input->policy_version;
    ticket.state_version=input->state_version;
    for(size_t i=0;i<input->omitted_count;++i) {
      if(!input->omitted_refs[i]) { copy_text(error,error_cap,"RETRIEVAL_TICKET_REF_INVALID"); return 0; }
      ticket.omitted_refs.emplace_back(input->omitted_refs[i]);
    }
    std::string message;
    const bool ok=static_cast<Handle*>(opaque)->tickets.issue(ticket,message);
    copy_text(error,error_cap,message);
    return ok?1:0;
  } catch(const std::exception& ex){copy_text(error,error_cap,ex.what());return 0;}
    catch(...){copy_text(error,error_cap,"UNKNOWN_RETRIEVAL_TICKET_EXCEPTION");return 0;}
}
int surge_retrieval_ticket_resolve(void* opaque, const char* ticket_id,
    uint64_t current_version, int replayable, const char* const* refs, size_t ref_count,
    char* output, size_t output_cap, size_t* required, char* error, size_t error_cap) {
  try {
    if(!opaque||!ticket_id||(replayable!=0&&replayable!=1)||(ref_count&&!refs)||ref_count>100000) {
      copy_text(error,error_cap,"RETRIEVAL_TICKET_RESOLVE_ARGUMENT_INVALID");return 0;
    }
    std::vector<std::string> reevaluated;
    for(size_t i=0;i<ref_count;++i){if(!refs[i]){copy_text(error,error_cap,"RETRIEVAL_TICKET_REF_INVALID");return 0;}reevaluated.emplace_back(refs[i]);}
    std::string message;
    const auto result=static_cast<Handle*>(opaque)->tickets.resolve(ticket_id,current_version,replayable==1,reevaluated,message);
    if(!message.empty()){copy_text(error,error_cap,message);return 0;}
    auto quote=[](const std::string& value){std::string s="\"";for(unsigned char c:value){if(c=='\"'||c=='\\'){s+='\\';s+=static_cast<char>(c);}else if(c=='\n')s+="\\n";else if(c=='\r')s+="\\r";else if(c=='\t')s+="\\t";else if(c<0x20)s+=' ';else s+=static_cast<char>(c);}s+='\"';return s;};
    std::ostringstream json;
    const char* status=result.status==surge::TicketStatus::Resolved?"RESOLVED":"STALE";
    json<<"{\"status\":\""<<status<<"\",\"resolved_state_version\":"<<result.resolved_state_version<<",\"refs\":[";
    for(size_t i=0;i<result.refs.size();++i){if(i)json<<',';json<<quote(result.refs[i]);}
    json<<"]}";
    const std::string payload=json.str();
    if(required)*required=payload.size()+1;
    if(!output||output_cap<payload.size()+1){copy_text(error,error_cap,"RETRIEVAL_BUFFER_TOO_SMALL");return 0;}
    copy_text(output,output_cap,payload);copy_text(error,error_cap,"");return 1;
  } catch(const std::exception& ex){copy_text(error,error_cap,ex.what());return 0;}
    catch(...){copy_text(error,error_cap,"UNKNOWN_RETRIEVAL_TICKET_EXCEPTION");return 0;}
}

int surge_packet_core_admit_and_apply(
    void* opaque,const SurgeRecognitionPacketInput* input,
    const unsigned char* snapshot,size_t snapshot_size,
    const char* delta_id,const char* semantic_idempotency_key,uint64_t expected,
    const SurgeGraphNodeInput* nodes,size_t node_count,
    const SurgeGraphRelationInput* relations,size_t relation_count,
    const SurgeScopeSpanInput* scopes,size_t scope_count,
    const char* focus_cluster_id,const char* focus_interest_id,
    int* duplicate,uint64_t* state_version,char* status,size_t status_cap,
    char* error,size_t error_cap) {
  try {
    if(!opaque||!input||!delta_id||!semantic_idempotency_key||
       !input->packet_id||!input->idempotency_key||!input->context_id||
       !input->source_id||!input->source_generation||!input->block_ref||
       !input->offset_basis||!input->source_snapshot_digest||
       !input->source_range_digest||!input->canonical_request_digest||
       (!snapshot&&snapshot_size)|| (input->revision!=0&&input->revision!=1)||
       (node_count&&!nodes)||(relation_count&&!relations)||(scope_count&&!scopes)||
       node_count>4096||relation_count>8192||scope_count>4096) {
      copy_text(error,error_cap,"C_API_ATOMIC_ARGUMENT_INVALID");return 0;
    }
    auto* handle=static_cast<Handle*>(opaque);
    surge::ValidatedRecognitionPacket packet;
    packet.packet_id=input->packet_id;packet.idempotency_key=input->idempotency_key;
    packet.context_id=input->context_id;packet.source_id=input->source_id;
    packet.source_generation=input->source_generation;packet.block_ref=input->block_ref;
    packet.offset_basis=input->offset_basis;packet.start_offset=input->start_offset;
    packet.end_offset=input->end_offset;packet.source_sequence=input->source_sequence;
    packet.source_snapshot_digest=input->source_snapshot_digest;
    packet.source_range_digest=input->source_range_digest;
    packet.canonical_request_digest=input->canonical_request_digest;
    packet.revision_of_packet_id=input->revision_of_packet_id?input->revision_of_packet_id:"";
    packet.revision=input->revision==1;packet.revision_sequence=input->revision_sequence;
    const std::string source=snapshot_size?std::string(reinterpret_cast<const char*>(snapshot),snapshot_size):std::string();

    surge::SemanticDelta delta;delta.delta_id=delta_id;
    delta.idempotency_key=semantic_idempotency_key;delta.expected_state_version=expected;
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
    const std::string packet_ref=input->packet_id;
    for(size_t i=0;i<scope_count;++i){
      const auto& scope_input=scopes[i];
      if(!scope_input.span_id||!scope_input.kind||!scope_input.source_digest||
         !scope_input.offset_basis||(scope_input.has_closer!=0&&scope_input.has_closer!=1)||
         (scope_input.target_count&&!scope_input.target_refs)||
         (scope_input.antecedent_count&&!scope_input.antecedent_refs)||
         (scope_input.packet_count&&!scope_input.packet_refs)||
         scope_input.target_count>1024||scope_input.antecedent_count>1024||
         scope_input.packet_count>4096){
        copy_text(error,error_cap,"C_API_SCOPE_ARGUMENT_INVALID");return 0;
      }
      surge::ScopeSpanProposal span;
      span.span_id=scope_input.span_id;
      if(!scope_kind(scope_input.kind,span.kind)){
        copy_text(error,error_cap,"C_API_SCOPE_KIND_INVALID");return 0;
      }
      span.source_digest=scope_input.source_digest;
      span.offset_basis=scope_input.offset_basis;
      span.opener=scope_anchor(scope_input.opener);
      if(scope_input.has_closer)span.closer=scope_anchor(scope_input.closer);
      for(size_t j=0;j<scope_input.target_count;++j){
        if(!scope_input.target_refs[j]){copy_text(error,error_cap,"C_API_SCOPE_TARGET_INVALID");return 0;}
        span.target_refs.emplace_back(scope_input.target_refs[j]);
      }
      for(size_t j=0;j<scope_input.antecedent_count;++j){
        if(!scope_input.antecedent_refs[j]){copy_text(error,error_cap,"C_API_SCOPE_ANTECEDENT_INVALID");return 0;}
        span.antecedent_refs.emplace_back(scope_input.antecedent_refs[j]);
      }
      for(size_t j=0;j<scope_input.packet_count;++j){
        if(!scope_input.packet_refs[j]){copy_text(error,error_cap,"C_API_SCOPE_PACKET_INVALID");return 0;}
        span.packet_refs.emplace_back(scope_input.packet_refs[j]);
      }
      if(std::find(span.packet_refs.begin(),span.packet_refs.end(),packet_ref)==span.packet_refs.end()){
        copy_text(error,error_cap,"SCOPE_CURRENT_PACKET_REFERENCE_REQUIRED");return 0;
      }
      span.attribution_ref=scope_input.attribution_ref?scope_input.attribution_ref:"";
      span.modality_kind=scope_input.modality_kind?scope_input.modality_kind:"";
      span.temporal_role=scope_input.temporal_role?scope_input.temporal_role:"";
      span.parent_ref=scope_input.parent_ref?scope_input.parent_ref:"";
      delta.scope_spans.push_back({std::move(span),source});
    }
    if(focus_cluster_id&&*focus_cluster_id)
      delta.focus=surge::SetFocus{{focus_cluster_id,focus_interest_id?focus_interest_id:""}};
    std::vector<surge::JournalEvent> semantic_events;std::string prepare_error;
    if(!handle->graph.prepare_events(delta,semantic_events,prepare_error)){
      copy_text(error,error_cap,prepare_error);return 0;
    }
    const auto admitted=handle->coordinator.admit(packet,source,semantic_events);
    if(!admitted.admitted){copy_text(error,error_cap,admitted.error);return 0;}
    if(!handle->graph.open()){
      copy_text(error,error_cap,"ATOMIC_COMMIT_SUCCEEDED_GRAPH_REPLAY_FAILED");return 0;
    }
    if(duplicate)*duplicate=admitted.duplicate?1:0;
    if(state_version)*state_version=handle->coordinator.state_version();
    copy_text(status,status_cap,admitted.packet_status);
    copy_text(error,error_cap,"");
    return 1;
  } catch(const std::exception& ex){copy_text(error,error_cap,ex.what());return 0;}
    catch(...){copy_text(error,error_cap,"UNKNOWN_ATOMIC_COMMIT_EXCEPTION");return 0;}
}

int surge_question_aspect_set_status(void* opaque, const char* aspect_id,
    const char* status, uint64_t expected_state_version, char* error, size_t error_capacity) {
  try {
    if (!opaque || !aspect_id || !*aspect_id || !status) {
      copy_text(error, error_capacity, "C_API_ARGUMENT_INVALID"); return 0;
    }
    surge::QuestionAspectStatus value;
    const std::string name(status);
    if (name == "OPEN") value = surge::QuestionAspectStatus::Open;
    else if (name == "RESOLVED") value = surge::QuestionAspectStatus::Resolved;
    else if (name == "DEFERRED") value = surge::QuestionAspectStatus::Deferred;
    else { copy_text(error, error_capacity, "QUESTION_ASPECT_STATUS_INVALID"); return 0; }
    std::string message;
    auto& graph = static_cast<Handle*>(opaque)->graph;
    const bool ok = graph.set_question_aspect_status(aspect_id, value, expected_state_version, message);
    copy_text(error, error_capacity, message);
    return ok ? 1 : 0;
  } catch (const std::exception& ex) { copy_text(error, error_capacity, ex.what()); return 0; }
    catch (...) { copy_text(error, error_capacity, "UNKNOWN_QUESTION_ASPECT_STATUS_EXCEPTION"); return 0; }
}
uint64_t surge_packet_core_state_version(void* opaque) {
  return opaque?static_cast<Handle*>(opaque)->coordinator.state_version():0;
}
size_t surge_packet_core_packet_count(void* opaque) {
  return opaque?static_cast<Handle*>(opaque)->coordinator.packet_count():0;
}
size_t surge_context_graph_scope_count(void* opaque) {
  return opaque?static_cast<Handle*>(opaque)->graph.scope_spans().size():0;
}

int surge_context_graph_retrieve(void* opaque, const char* query, const char* kinds_csv,
    size_t limit, char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity) {
  try {
    if (!opaque || !query || !*query || limit == 0 || limit > 1000) {
      copy_text(error, error_capacity, "RETRIEVAL_ARGUMENT_INVALID"); return 0;
    }
    auto* handle = static_cast<Handle*>(opaque);
    std::set<std::string> allowed;
    if (kinds_csv && *kinds_csv) {
      std::istringstream filters(kinds_csv);
      std::string item;
      while (std::getline(filters, item, ',')) {
        if (!item.empty()) allowed.insert(item);
      }
    }
    auto lower = [](std::string value) {
      for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      return value;
    };
    const std::string needle = lower(query);
    auto score_text = [&](const std::string& value) -> int {
      const std::string hay = lower(value);
      if (needle == "*") return 1;
      if (hay.empty()) return 0;
      if (hay == needle) return 100;
      if (hay.find(needle) != std::string::npos) return 85;
      std::istringstream terms(needle);
      std::string term;
      int total = 0, hits = 0;
      while (terms >> term) { ++total; if (hay.find(term) != std::string::npos) ++hits; }
      if (total == 0 || hits == 0) return 0;
      return hits == total ? 65 + hits : 20 + (40 * hits / total);
    };
    auto quote = [](const std::string& value) {
      std::string out = "\"";
      for (unsigned char ch : value) {
        switch (ch) {
          case '\"': out += "\\\""; break;
          case '\\': out += "\\\\"; break;
          case '\b': out += "\\b"; break;
          case '\f': out += "\\f"; break;
          case '\n': out += "\\n"; break;
          case '\r': out += "\\r"; break;
          case '\t': out += "\\t"; break;
          default:
            if (ch < 0x20) {
              const char hex[] = "0123456789abcdef";
              out += "\\u00"; out += hex[(ch >> 4) & 0x0f]; out += hex[ch & 0x0f];
            } else out += static_cast<char>(ch);
        }
      }
      out += "\"";
      return out;
    };
    struct Hit { int score; std::string kind, id, json; };
    std::vector<Hit> hits;
    const auto& graph = handle->graph;
    auto kind_allowed = [&](const std::string& kind) {
      return allowed.empty() || allowed.count(kind) != 0;
    };
    auto node_kind_text = [](surge::NodeKind kind) {
      switch (kind) {
        case surge::NodeKind::Cluster: return "CLUSTER";
        case surge::NodeKind::Entity: return "ENTITY";
        case surge::NodeKind::Evidence: return "EVIDENCE";
        case surge::NodeKind::Claim: return "CLAIM";
        case surge::NodeKind::Question: return "QUESTION";
        case surge::NodeKind::QuestionAspect: return "QUESTION_ASPECT";
        case surge::NodeKind::Dependency: return "DEPENDENCY";
        case surge::NodeKind::Interest: return "INTEREST";
      }
      return "UNKNOWN";
    };
    auto relation_kind_text = [](surge::RelationKind kind) {
      switch (kind) {
        case surge::RelationKind::Supports: return "SUPPORTS";
        case surge::RelationKind::Contradicts: return "CONTRADICTS";
        case surge::RelationKind::AgreesWith: return "AGREES_WITH";
        case surge::RelationKind::ConflictsWith: return "CONFLICTS_WITH";
        case surge::RelationKind::DependsOn: return "DEPENDS_ON";
        case surge::RelationKind::About: return "ABOUT";
      }
      return "UNKNOWN";
    };
    for (const auto& entry : graph.nodes()) {
      const auto& node = entry.second;
      const std::string kind = node_kind_text(node.kind);
      if (!kind_allowed(kind)) continue;
      int score = std::max(score_text(node.content), score_text(node.id));
      if (score == 0) continue;
      std::ostringstream item;
      item << "{\"kind\":" << quote(kind) << ",\"id\":" << quote(node.id)
           << ",\"content\":" << quote(node.content) << ",\"score\":" << score
           << ",\"references\":[";
      for (size_t i = 0; i < node.references.size(); ++i) {
        if (i) item << ',';
        item << quote(node.references[i]);
      }
      item << "],";
      if (node.kind == surge::NodeKind::QuestionAspect) {
        const auto status = graph.question_aspect_status(node.id);
        const char* status_text = status == surge::QuestionAspectStatus::Open ? "OPEN" :
            status == surge::QuestionAspectStatus::Resolved ? "RESOLVED" : "DEFERRED";
        item << "\"status\":" << quote(status_text) << ",";
      }
      item << "\"relations\":[";
      bool first = true;
      for (const auto& rel_entry : graph.relations()) {
        const auto& rel = rel_entry.second;
        if (rel.from != node.id && rel.to != node.id) continue;
        if (!first) item << ',';
        first = false;
        const std::string other = rel.from == node.id ? rel.to : rel.from;
        item << "{\"kind\":" << quote(relation_kind_text(rel.kind))
             << ",\"ref\":" << quote(other) << "}";
      }
      item << "]}";
      hits.push_back({score, kind, node.id, item.str()});
    }
    for (const auto& entry : graph.scope_spans()) {
      const auto& record = entry.second;
      const auto& span = record.proposal;
      if (!kind_allowed("SCOPE_SPAN")) continue;
      std::string searchable = span.span_id + " " + span.opener.exact_text + " " +
          span.attribution_ref + " " + span.modality_kind + " " + span.temporal_role +
          " " + span.parent_ref;
      for (const auto& ref : span.target_refs) searchable += " " + ref;
      for (const auto& ref : span.antecedent_refs) searchable += " " + ref;
      int score = score_text(searchable);
      if (score == 0) continue;
      std::ostringstream item;
      item << "{\"kind\":\"SCOPE_SPAN\",\"id\":" << quote(span.span_id)
           << ",\"scope_kind\":" << quote([&]() {
                switch (span.kind) {
                  case surge::ScopeKind::Negation: return "NEGATION";
                  case surge::ScopeKind::Condition: return "CONDITION";
                  case surge::ScopeKind::Modality: return "MODALITY";
                  case surge::ScopeKind::Concession: return "CONCESSION";
                  case surge::ScopeKind::Contrast: return "CONTRAST";
                  case surge::ScopeKind::Coreference: return "COREFERENCE";
                  case surge::ScopeKind::Anaphora: return "ANAPHORA";
                  case surge::ScopeKind::Quotation: return "QUOTATION";
                  case surge::ScopeKind::Temporal: return "TEMPORAL";
                  case surge::ScopeKind::ListContinuation: return "LIST_CONTINUATION";
                  case surge::ScopeKind::TableHeader: return "TABLE_HEADER";
                }
                return "UNKNOWN";
              }())
           << ",\"opener_text\":" << quote(span.opener.exact_text)
           << ",\"status\":" << quote(record.status == surge::ScopeStatus::Closed ? "CLOSED" : "UNRESOLVED")
           << ",\"effect\":" << quote([&]() {
                switch (record.effect) {
                  case surge::ScopeEffect::NegatedClaim: return "NEGATED_CLAIM";
                  case surge::ScopeEffect::ConditionalRelation: return "CONDITIONAL_RELATION";
                  case surge::ScopeEffect::ModalityQualified: return "MODALITY_QUALIFIED";
                  case surge::ScopeEffect::ConcessionRelation: return "CONCESSION_RELATION";
                  case surge::ScopeEffect::ContrastRelation: return "CONTRAST_RELATION";
                  case surge::ScopeEffect::ReferenceCandidate: return "REFERENCE_CANDIDATE";
                  case surge::ScopeEffect::QuotationEvidence: return "QUOTATION_EVIDENCE";
                  case surge::ScopeEffect::TemporalQualifier: return "TEMPORAL_QUALIFIER";
                  case surge::ScopeEffect::StructuralContinuation: return "STRUCTURAL_CONTINUATION";
                  case surge::ScopeEffect::TableHeaderScope: return "TABLE_HEADER_SCOPE";
                }
                return "UNKNOWN";
              }())
           << ",\"may_emit_unconditional_claim\":"
           << (record.may_emit_unconditional_claim ? "true" : "false")
           << ",\"target_refs\":[";
      for (size_t i = 0; i < span.target_refs.size(); ++i) {
        if (i) item << ',';
        item << quote(span.target_refs[i]);
      }
      item << "],\"antecedent_refs\":[";
      for (size_t i = 0; i < span.antecedent_refs.size(); ++i) {
        if (i) item << ',';
        item << quote(span.antecedent_refs[i]);
      }
      item << "],\"packet_refs\":[";
      for (size_t i = 0; i < span.packet_refs.size(); ++i) {
        if (i) item << ',';
        item << quote(span.packet_refs[i]);
      }
      item << "],\"source_digest\":" << quote(span.source_digest)
           << ",\"offset_basis\":" << quote(span.offset_basis)
           << ",\"score\":" << score << "}";
      hits.push_back({score, "SCOPE_SPAN", span.span_id, item.str()});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
      if (a.score != b.score) return a.score > b.score;
      if (a.kind != b.kind) return a.kind < b.kind;
      return a.id < b.id;
    });
    std::ostringstream result;
    result << "{\"state_version\":" << graph.state_version() << ",\"query\":"
           << quote(query) << ",\"focus\":";
    if (graph.focus()) {
      result << "{\"cluster_id\":" << quote(graph.focus()->cluster_id)
             << ",\"interest_id\":" << quote(graph.focus()->interest_id) << "}";
    } else result << "null";
    result << ",\"items\":[";
    const size_t count = std::min(limit, hits.size());
    for (size_t i = 0; i < count; ++i) {
      if (i) result << ',';
      result << hits[i].json;
    }
    result << "],\"total_matches\":" << hits.size() << "}";
    const std::string payload = result.str();
    if (required_capacity) *required_capacity = payload.size() + 1;
    if (!output || output_capacity < payload.size() + 1) {
      copy_text(error, error_capacity, "RETRIEVAL_BUFFER_TOO_SMALL"); return 0;
    }
    copy_text(output, output_capacity, payload);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) {
    copy_text(error, error_capacity, ex.what()); return 0;
  } catch (...) {
    copy_text(error, error_capacity, "UNKNOWN_RETRIEVAL_EXCEPTION"); return 0;
  }
}


int surge_context_graph_dependency_evaluation(void* opaque, const char* delta_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity) {
  try {
    if (!opaque || !delta_id || !*delta_id) {
      copy_text(error, error_capacity, "DEPENDENCY_EVALUATION_ARGUMENT_INVALID");
      return 0;
    }
    const auto& records = static_cast<Handle*>(opaque)->graph.dependency_evaluations();
    const auto found = records.find(delta_id);
    if (found == records.end()) {
      copy_text(error, error_capacity, "DEPENDENCY_EVALUATION_NOT_FOUND");
      return 0;
    }
    auto quote = [](const std::string& value) {
      std::string out = "\"";
      for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) out += ' ';
        else out += static_cast<char>(c);
      }
      out += '"';
      return out;
    };
    const auto& record = found->second;
    std::ostringstream json;
    json << "{\"delta_id\":" << quote(record.delta_id)
         << ",\"status\":" << quote(record.status)
         << ",\"iterations\":" << record.iterations
         << ",\"evaluation_cost\":" << record.evaluation_cost
         << ",\"diagnostic_error\":" << quote(record.diagnostic_error)
         << ",\"triggered\":[";
    for (size_t i = 0; i < record.triggered.size(); ++i) {
      if (i) json << ',';
      const auto& trigger = record.triggered[i];
      json << "{\"dependency_id\":" << quote(trigger.dependency_id)
           << ",\"prerequisite_id\":" << quote(trigger.prerequisite_id)
           << ",\"dependent_id\":" << quote(trigger.dependent_id) << '}';
    }
    json << "],\"impacted_nodes\":[";
    for (size_t i = 0; i < record.impacted_nodes.size(); ++i) {
      if (i) json << ',';
      json << quote(record.impacted_nodes[i]);
    }
    json << "],\"rejected_dependencies\":[";
    for (size_t i = 0; i < record.rejected_dependencies.size(); ++i) {
      if (i) json << ',';
      json << quote(record.rejected_dependencies[i]);
    }
    json << "],\"cyclic_components\":[";
    for (size_t i = 0; i < record.cyclic_components.size(); ++i) {
      if (i) json << ',';
      json << '[';
      for (size_t j = 0; j < record.cyclic_components[i].size(); ++j) {
        if (j) json << ',';
        json << quote(record.cyclic_components[i][j]);
      }
      json << ']';
    }
    json << "]}";
    const std::string payload = json.str();
    if (required_capacity) *required_capacity = payload.size() + 1;
    if (!output || output_capacity < payload.size() + 1) {
      copy_text(error, error_capacity, "RETRIEVAL_BUFFER_TOO_SMALL");
      return 0;
    }
    copy_text(output, output_capacity, payload);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) {
    copy_text(error, error_capacity, ex.what()); return 0;
  } catch (...) {
    copy_text(error, error_capacity, "UNKNOWN_DEPENDENCY_EVALUATION_EXCEPTION");
    return 0;
  }
}
}
