#include "surge/c_api.h"
#include "surge/context_graph.hpp"
#include "surge/budgeted_context_graph.hpp"
#include "surge/resource_budget.hpp"
#include "surge/chaos_cluster.hpp"
#include "surge/context_hint.hpp"
#include "surge/event_journal.hpp"
#include "surge/evidence_provenance.hpp"
#include "surge/packet_admission.hpp"
#include "surge/temporal_boundary_ledger.hpp"
#include "surge/temporal_question_ledger.hpp"
#include "surge/question_router.hpp"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <exception>
#include <memory>
#include <limits>
#include <string>
#include <sstream>
#include <set>
#include <vector>
#include <cctype>
#include <cmath>

namespace {
std::string json_quote(const std::string& value) {
  std::string out = "\"";
  for (unsigned char ch : value) {
    switch (ch) {
      case '"': out += "\\\""; break;
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
}
void copy_text(char* out, size_t capacity, const std::string& value) {
  if (!out || capacity == 0) return;
  const auto count = std::min(capacity - 1, value.size());
  std::memcpy(out, value.data(), count);
  out[count] = '\0';
}
std::string temporal_pending_json(const surge::TemporalBoundaryPending& p) {
  std::ostringstream j;
  j << R"({"claim_id":)" << json_quote(p.claim_id)
    << R"(,"valid_from":)" << json_quote(p.valid_from)
    << R"(,"valid_until":)" << json_quote(p.valid_until)
    << R"(,"status":)" << json_quote(p.status) << R"(,"evidence_ids":[)";
  for (size_t i=0;i<p.evidence_ids.size();++i) { if(i) j << ','; j << json_quote(p.evidence_ids[i]); }
  j << R"(],"packet_ids":[)";
  for (size_t i=0;i<p.packet_ids.size();++i) { if(i) j << ','; j << json_quote(p.packet_ids[i]); }
  j << R"(],"candidates":[)";
  for (size_t i=0;i<p.candidates.size();++i) {
    if(i) j << ',';
    const auto& x=p.candidates[i];
    j << R"({"candidate_id":)" << json_quote(x.candidate_id)
      << R"(,"role":)" << json_quote(x.role)
      << R"(,"valid_from":)" << json_quote(x.valid_from)
      << R"(,"valid_until":)" << json_quote(x.valid_until)
      << R"(,"evidence_id":)" << json_quote(x.evidence_id)
      << R"(,"packet_id":)" << json_quote(x.packet_id)
      << R"(,"selected":)" << (x.selected?"true":"false") << '}';
  }
  j << "]}";
  return j.str();
}
std::string temporal_question_json(const surge::TemporalQuestion& q) {
  std::ostringstream j;
  j << R"({"question_id":)" << json_quote(q.question_id)
    << R"(,"claim_id":)" << json_quote(q.claim_id)
    << R"(,"role":)" << json_quote(q.role)
    << R"(,"status":)" << json_quote(q.status)
    << R"(,"last_event_id":)" << json_quote(q.last_event_id)
    << R"(,"candidate_ids":[)";
  for (size_t i=0;i<q.candidate_ids.size();++i) { if(i) j << ','; j << json_quote(q.candidate_ids[i]); }
  j << R"(],"evidence_ids":[)";
  for (size_t i=0;i<q.evidence_ids.size();++i) { if(i) j << ','; j << json_quote(q.evidence_ids[i]); }
  j << R"(],"packet_ids":[)";
  for (size_t i=0;i<q.packet_ids.size();++i) { if(i) j << ','; j << json_quote(q.packet_ids[i]); }
  j << "]}";
  return j.str();
}
std::string temporal_result_json(const surge::TemporalBoundaryLedgerResult& r) {
  std::ostringstream j;
  j << R"({"committed":)" << (r.committed?"true":"false")
    << R"(,"duplicate":)" << (r.duplicate?"true":"false")
    << R"(,"state_version":)" << r.state_version
    << R"(,"status":)" << json_quote(r.status)
    << R"(,"error":)" << json_quote(r.error)
    << R"(,"pending":)" << temporal_pending_json(r.pending) << '}';
  return j.str();
}
bool node_kind(const char* value, surge::NodeKind& out) {
  if (!value) return false;
  const std::string s(value);
  if(s=="CLUSTER")out=surge::NodeKind::Cluster;
  else if(s=="CHAOS_CLUSTER")out=surge::NodeKind::ChaosCluster;
  else if(s=="ENTITY")out=surge::NodeKind::Entity;
  else if(s=="EVIDENCE")out=surge::NodeKind::Evidence;
  else if(s=="CLAIM")out=surge::NodeKind::Claim;
  else if(s=="QUESTION")out=surge::NodeKind::Question;
  else if(s=="QUESTION_ASPECT")out=surge::NodeKind::QuestionAspect;
  else if(s=="DEPENDENCY")out=surge::NodeKind::Dependency;
  else if(s=="INTEREST")out=surge::NodeKind::Interest;
  else if(s=="CLUSTER_LIFECYCLE")out=surge::NodeKind::ClusterLifecycle;
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
  else if(s=="SUPERSEDES")out=surge::RelationKind::Supersedes;
  else if(s=="RETRACTS")out=surge::RelationKind::Retracts;
  else if(s=="DUPLICATES")out=surge::RelationKind::Duplicates;
  else if(s=="CONTAINS")out=surge::RelationKind::Contains;
  else return false;
  return true;
}
surge::ResourceBudgetLimits compatibility_budget_limits() {
  // Preserve the legacy C ABI's historical unbounded behavior.
  const auto unlimited = std::numeric_limits<std::size_t>::max();
  return {unlimited, unlimited, unlimited, unlimited, unlimited, unlimited};
}
struct Handle {
  surge::EventJournal journal;
  surge::PacketAdmissionCoordinator coordinator;
  surge::ContextGraph graph;
  surge::ResourceBudget graph_budget;
  surge::BudgetedContextGraph mutations;
  surge::PersistentRetrievalTicketRegistry tickets;
  surge::TemporalBoundaryLedger temporal_ledger;
  surge::TemporalQuestionLedger temporal_questions;
  surge::EvidenceProvenanceLedger evidence_provenance;
  surge::ChaosClusterLedger chaos;
  explicit Handle(const std::string& path, surge::ResourceBudgetLimits limits)
      : journal(path), coordinator(journal), graph(journal),
        graph_budget(limits), mutations(graph, graph_budget), tickets(journal),
        temporal_ledger(journal), temporal_questions(journal),
        evidence_provenance(journal), chaos(journal) {}
};
bool ensure_temporal_question_route(Handle& handle,const surge::TemporalQuestion& question){
  const std::string aspect_id="aspect/"+question.question_id;
  const auto existing=handle.graph.nodes().find(question.question_id);
  if(existing!=handle.graph.nodes().end())
    return existing->second.kind==surge::NodeKind::Question &&
           handle.graph.nodes().count(aspect_id)!=0;
  std::set<std::string> clusters;
  for(const auto& relation:handle.graph.relations()){
    if(relation.second.kind==surge::RelationKind::About &&
       relation.second.from==question.claim_id &&
       handle.graph.nodes().count(relation.second.to) &&
       handle.graph.nodes().at(relation.second.to).kind==surge::NodeKind::Cluster)
      clusters.insert(relation.second.to);
  }
  surge::SemanticDelta delta;
  delta.delta_id="temporal-question-route/"+question.question_id;
  delta.idempotency_key=delta.delta_id;
  delta.expected_state_version=handle.journal.state_version();
  delta.nodes.push_back({{question.question_id,surge::NodeKind::Question,
    "時間境界候補の競合を確認する: "+question.claim_id+" / "+question.role,{}}});
  delta.nodes.push_back({{aspect_id,surge::NodeKind::QuestionAspect,
    "候補の適用時点・根拠・有効範囲を確認する。",{question.question_id}}});
  std::vector<std::string> interests;
  for(const auto& cluster:clusters){
    const auto interest_id="interest/temporal/"+question.question_id+"/"+cluster;
    interests.push_back(interest_id);
    delta.nodes.push_back({{interest_id,surge::NodeKind::Interest,
      "時間境界の根拠を確認する対象クラスター",{aspect_id,cluster}}});
  }
  if(clusters.size()==1)delta.focus=surge::SetFocus{{*clusters.begin(),interests.front()}};
  const auto result=handle.mutations.apply(delta);
  return result.committed;
}
}
extern "C" {
uint32_t surge_abi_version(void) {
  return SURGE_ABI_VERSION_ENCODE(SURGE_ABI_VERSION_MAJOR,
                                 SURGE_ABI_VERSION_MINOR,
                                 SURGE_ABI_VERSION_PATCH);
}

void* surge_packet_core_open_with_limits(const char* path,
    const SurgeResourceBudgetLimits* limits, char* error, size_t cap) {
  try {
    if (!path || !*path) { copy_text(error, cap, "JOURNAL_PATH_REQUIRED"); return nullptr; }
    if (!limits) { copy_text(error, cap, "RESOURCE_BUDGET_LIMITS_REQUIRED"); return nullptr; }
    const surge::ResourceBudgetLimits budget_limits{
      limits->resident_bytes, limits->transient_bytes, limits->retained_events,
      limits->retained_clusters, limits->decision_candidates, limits->decision_evidence};
    auto handle = std::make_unique<Handle>(path, budget_limits);
    if (!handle->journal.open()) { copy_text(error, cap, handle->journal.last_error()); return nullptr; }
    if (!handle->coordinator.recover()) { copy_text(error, cap, "PACKET_COORDINATOR_RECOVERY_FAILED"); return nullptr; }
    if (!handle->graph.open()) { copy_text(error, cap, "CONTEXT_GRAPH_RECOVERY_FAILED"); return nullptr; }
    std::string graph_budget_error;
    if (!handle->mutations.recover(graph_budget_error)) {
      copy_text(error, cap, graph_budget_error); return nullptr;
    }
    if (!handle->tickets.open()) { copy_text(error, cap, "RETRIEVAL_TICKET_RECOVERY_FAILED"); return nullptr; }
    if (!handle->temporal_ledger.replay()) {
      copy_text(error, cap, handle->temporal_ledger.last_error()); return nullptr;
    }
    if (!handle->temporal_questions.replay()) {
      copy_text(error, cap, handle->temporal_questions.last_error()); return nullptr;
    }
    if (!handle->chaos.open()) { copy_text(error, cap, "CHAOS_CLUSTER_RECOVERY_FAILED"); return nullptr; }
    if (!handle->evidence_provenance.replay()) {
      copy_text(error, cap, "EVIDENCE_PROVENANCE_RECOVERY_FAILED"); return nullptr;
    }
    copy_text(error, cap, "");
    return handle.release();
  } catch (const std::exception& ex) { copy_text(error, cap, ex.what()); return nullptr; }
    catch (...) { copy_text(error, cap, "UNKNOWN_CORE_EXCEPTION"); return nullptr; }
}
void* surge_packet_core_open(const char* path, char* error, size_t cap) {
  const auto limits = compatibility_budget_limits();
  const SurgeResourceBudgetLimits c_limits{
    limits.resident_bytes, limits.transient_bytes, limits.retained_events,
    limits.retained_clusters, limits.decision_candidates, limits.decision_evidence};
  return surge_packet_core_open_with_limits(path, &c_limits, error, cap);
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
    const auto result=static_cast<Handle*>(opaque)->mutations.apply(delta);
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
    const auto result=static_cast<Handle*>(opaque)->mutations.apply(delta);
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

int surge_packet_core_admit_and_apply_with_metadata(
    void* opaque,const SurgeRecognitionPacketInput* input,
    const unsigned char* snapshot,size_t snapshot_size,
    const char* delta_id,const char* semantic_idempotency_key,uint64_t expected,
    const SurgeGraphNodeInput* nodes,size_t node_count,
    const SurgeGraphRelationInput* relations,size_t relation_count,
    const SurgeScopeSpanInput* scopes,size_t scope_count,
    const char* focus_cluster_id,const char* focus_interest_id,
    const SurgeClassificationProposalInput* proposal,
    const SurgeEvidenceProvenanceInput* provenance,
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
    if(proposal){
      if(!proposal->proposal_id||!*proposal->proposal_id||
         !proposal->packet_ref||!proposal->packet_source_generation||
         !proposal->serialized_json||!*proposal->serialized_json||
         std::string(proposal->packet_ref)!=packet.packet_id||
         proposal->base_state_version!=expected||
         proposal->context_hint_state_version>expected||
         std::string(proposal->packet_source_generation)!=packet.source_generation||
         proposal->packet_source_sequence!=packet.source_sequence||
         std::strlen(proposal->proposal_id)>256||
         std::strlen(proposal->serialized_json)>1024U*1024U){
        copy_text(error,error_cap,"CLASSIFICATION_PROPOSAL_LINEAGE_INVALID");return 0;
      }
      std::ostringstream payload;
      payload<<"P1 "<<std::quoted(std::string(proposal->proposal_id))<<' '
             <<std::quoted(std::string(proposal->packet_ref))<<' '
             <<proposal->base_state_version<<' '
             <<proposal->context_hint_state_version<<' '
             <<std::quoted(std::string(proposal->packet_source_generation))<<' '
             <<proposal->packet_source_sequence<<' '
             <<std::quoted(std::string(proposal->serialized_json));
      semantic_events.push_back({"classification-proposal:"+std::string(proposal->proposal_id),
          "CLASSIFICATION_PROPOSAL_RECORDED",payload.str()});
    }
    if(provenance){
      if(!provenance->evidence_id||!*provenance->evidence_id||
         std::strlen(provenance->evidence_id)>256||provenance->revision==0||
         (provenance->observed_at&&!*provenance->observed_at)||
         (provenance->source_published_at&&!*provenance->source_published_at)||
         (provenance->source_retrieved_at&&!*provenance->source_retrieved_at)){
        copy_text(error,error_cap,"EVIDENCE_PROVENANCE_ARGUMENT_INVALID");return 0;
      }
      const std::string evidence_id=provenance->evidence_id;
      bool evidence_exists=false;
      const auto staged=std::find_if(delta.nodes.begin(),delta.nodes.end(),
          [&](const surge::AddNode& item){return item.node.id==evidence_id;});
      if(staged!=delta.nodes.end()){
        evidence_exists=staged->node.kind==surge::NodeKind::Evidence;
      }else{
        const auto existing=handle->graph.nodes().find(evidence_id);
        evidence_exists=existing!=handle->graph.nodes().end()&&
                        existing->second.kind==surge::NodeKind::Evidence;
      }
      if(!evidence_exists){
        copy_text(error,error_cap,"EVIDENCE_PROVENANCE_TARGET_NOT_EVIDENCE");return 0;
      }
      surge::EvidenceProvenance value;
      value.evidence_id=evidence_id; value.revision=provenance->revision;
      if(provenance->observed_at)value.observed_at=provenance->observed_at;
      if(provenance->source_published_at)value.source_published_at=provenance->source_published_at;
      if(provenance->source_retrieved_at)value.source_retrieved_at=provenance->source_retrieved_at;
      surge::JournalEvent provenance_event;
      std::string provenance_error;
      if(!handle->evidence_provenance.prepare_event(value,expected,
            provenance_event,provenance_error)){
        copy_text(error,error_cap,provenance_error);return 0;
      }
      semantic_events.push_back(std::move(provenance_event));
    }
    const auto admitted=handle->coordinator.admit(packet,source,semantic_events);
    if(!admitted.admitted){copy_text(error,error_cap,admitted.error);return 0;}
    if(!handle->graph.open()){
      copy_text(error,error_cap,"ATOMIC_COMMIT_SUCCEEDED_GRAPH_REPLAY_FAILED");return 0;
    }
    if(provenance&&!handle->evidence_provenance.replay()){
      copy_text(error,error_cap,"ATOMIC_COMMIT_SUCCEEDED_PROVENANCE_REPLAY_FAILED");return 0;
    }
    if(duplicate)*duplicate=admitted.duplicate?1:0;
    if(state_version)*state_version=handle->coordinator.state_version();
    copy_text(status,status_cap,admitted.packet_status);
    copy_text(error,error_cap,"");
    return 1;
  } catch(const std::exception& ex){copy_text(error,error_cap,ex.what());return 0;}
    catch(...){copy_text(error,error_cap,"UNKNOWN_ATOMIC_COMMIT_EXCEPTION");return 0;}
}

int surge_packet_core_admit_and_apply_with_proposal(
    void* opaque,const SurgeRecognitionPacketInput* input,
    const unsigned char* snapshot,size_t snapshot_size,
    const char* delta_id,const char* semantic_idempotency_key,uint64_t expected,
    const SurgeGraphNodeInput* nodes,size_t node_count,
    const SurgeGraphRelationInput* relations,size_t relation_count,
    const SurgeScopeSpanInput* scopes,size_t scope_count,
    const char* focus_cluster_id,const char* focus_interest_id,
    const SurgeClassificationProposalInput* proposal,
    int* duplicate,uint64_t* state_version,char* status,size_t status_cap,
    char* error,size_t error_cap) {
  return surge_packet_core_admit_and_apply_with_metadata(
      opaque,input,snapshot,snapshot_size,delta_id,semantic_idempotency_key,expected,
      nodes,node_count,relations,relation_count,scopes,scope_count,
      focus_cluster_id,focus_interest_id,proposal,nullptr,duplicate,state_version,
      status,status_cap,error,error_cap);
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
  return surge_packet_core_admit_and_apply_with_proposal(
      opaque,input,snapshot,snapshot_size,delta_id,semantic_idempotency_key,expected,
      nodes,node_count,relations,relation_count,scopes,scope_count,
      focus_cluster_id,focus_interest_id,nullptr,duplicate,state_version,
      status,status_cap,error,error_cap);
}

int surge_classification_proposal_get(void* opaque,const char* proposal_id,
    char* output,size_t output_capacity,size_t* required_capacity,
    char* error,size_t error_capacity){
  try{
    if(!opaque||!proposal_id||!*proposal_id||(!output&&output_capacity)){
      copy_text(error,error_capacity,"CLASSIFICATION_PROPOSAL_QUERY_ARGUMENT_INVALID");return 0;
    }
    const auto events=static_cast<Handle*>(opaque)->journal.committed_events();
    for(auto it=events.rbegin();it!=events.rend();++it){
      if(it->kind!="CLASSIFICATION_PROPOSAL_RECORDED")continue;
      std::istringstream payload(it->payload);
      std::string magic,id,packet_ref,generation,json,extra;
      std::uint64_t base=0,hint=0,sequence=0;
      if(!(payload>>magic>>std::quoted(id)>>std::quoted(packet_ref)>>base>>hint>>
           std::quoted(generation)>>sequence>>std::quoted(json))||(payload>>extra)||magic!="P1"){
        copy_text(error,error_capacity,"CLASSIFICATION_PROPOSAL_JOURNAL_RECORD_INVALID");return 0;
      }
      if(id!=proposal_id)continue;
      if(packet_ref.empty()||generation.empty()||hint>base||json.empty()){
        copy_text(error,error_capacity,"CLASSIFICATION_PROPOSAL_JOURNAL_LINEAGE_INVALID");return 0;
      }
      const std::size_t required=json.size()+1;
      if(required_capacity)*required_capacity=required;
      if(!output||output_capacity<required){
        copy_text(error,error_capacity,"CLASSIFICATION_PROPOSAL_BUFFER_TOO_SMALL");return 0;
      }
      copy_text(output,output_capacity,json);
      copy_text(error,error_capacity,"");
      return 1;
    }
    copy_text(error,error_capacity,"CLASSIFICATION_PROPOSAL_NOT_FOUND");return 0;
  }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
   catch(...){copy_text(error,error_capacity,"UNKNOWN_CLASSIFICATION_PROPOSAL_QUERY_EXCEPTION");return 0;}
}

int surge_evidence_provenance_get(void* opaque,const char* evidence_id,
    char* output,size_t output_capacity,size_t* required_capacity,
    char* error,size_t error_capacity){
  try{
    if(!opaque||!evidence_id||!*evidence_id){
      copy_text(error,error_capacity,"EVIDENCE_PROVENANCE_QUERY_ARGUMENT_INVALID");return 0;
    }
    const auto& ledger=static_cast<Handle*>(opaque)->evidence_provenance;
    const auto found=ledger.history().find(evidence_id);
    if(found==ledger.history().end()){
      copy_text(error,error_capacity,"EVIDENCE_PROVENANCE_NOT_FOUND");return 0;
    }
    auto optional_json=[](const std::optional<std::string>& value){
      return value?json_quote(*value):std::string("null");
    };
    std::ostringstream json;
    json<<"{\"evidence_id\":"<<json_quote(evidence_id)<<",\"revisions\":[";
    for(std::size_t i=0;i<found->second.size();++i){
      if(i)json<<',';
      const auto& record=found->second[i];
      const auto& p=record.provenance;
      json<<"{\"revision\":"<<p.revision
          <<",\"recorded_state_version\":"<<record.recorded_state_version
          <<",\"observed_at\":"<<optional_json(p.observed_at)
          <<",\"source_published_at\":"<<optional_json(p.source_published_at)
          <<",\"source_retrieved_at\":"<<optional_json(p.source_retrieved_at)<<'}';
    }
    json<<"]}";
    const auto value=json.str();
    if(required_capacity)*required_capacity=value.size()+1;
    if(!output||output_capacity<value.size()+1){
      copy_text(error,error_capacity,"EVIDENCE_PROVENANCE_BUFFER_TOO_SMALL");return 0;
    }
    copy_text(output,output_capacity,value);copy_text(error,error_capacity,"");return 1;
  }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
   catch(...){copy_text(error,error_capacity,"UNKNOWN_EVIDENCE_PROVENANCE_QUERY_EXCEPTION");return 0;}
}

int surge_context_graph_apply_claim_validity(void* opaque,
    const char* delta_id, const char* idempotency_key, uint64_t expected_state_version,
    const char* claim_id, const char* valid_from, const char* valid_until,
    const char* basis_evidence_id, uint64_t revision,
    int* duplicate, uint64_t* state_version, char* error, size_t error_capacity) {
  try {
    if(!opaque||!delta_id||!idempotency_key||!claim_id||!valid_from||
       !basis_evidence_id||revision==0){
      copy_text(error,error_capacity,"CLAIM_VALIDITY_ARGUMENT_INVALID");return 0;
    }
    surge::SemanticDelta delta;
    delta.delta_id=delta_id;delta.idempotency_key=idempotency_key;
    delta.expected_state_version=expected_state_version;
    delta.claim_validity_updates.push_back({claim_id,valid_from,valid_until?valid_until:"",basis_evidence_id,revision,"",""});
    const auto result=static_cast<Handle*>(opaque)->mutations.apply(delta);
    if(duplicate)*duplicate=result.duplicate?1:0;
    if(state_version)*state_version=result.state_version;
    copy_text(error,error_capacity,result.error);
    return result.committed?1:0;
  } catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
    catch(...){copy_text(error,error_capacity,"UNKNOWN_CLAIM_VALIDITY_EXCEPTION");return 0;}
}

int surge_context_graph_apply_claim_validity_reviewed(void* opaque,
    const char* delta_id, const char* idempotency_key, uint64_t expected_state_version,
    const char* claim_id, const char* valid_from, const char* valid_until,
    const char* basis_evidence_id, uint64_t revision,
    const char* review_evidence_id, const char* reviewer_id,
    int* duplicate, uint64_t* state_version, char* error, size_t error_capacity) {
  try {
    if(!opaque||!delta_id||!idempotency_key||!claim_id||!valid_from||
       !basis_evidence_id||!review_evidence_id||!*review_evidence_id||
       !reviewer_id||!*reviewer_id||revision==0){
      copy_text(error,error_capacity,"CLAIM_VALIDITY_REVIEW_ARGUMENT_INVALID");return 0;
    }
    surge::SemanticDelta delta;
    delta.delta_id=delta_id;delta.idempotency_key=idempotency_key;
    delta.expected_state_version=expected_state_version;
    delta.claim_validity_updates.push_back({claim_id,valid_from,valid_until?valid_until:"",
        basis_evidence_id,revision,review_evidence_id,reviewer_id});
    const auto result=static_cast<Handle*>(opaque)->mutations.apply(delta);
    if(duplicate)*duplicate=result.duplicate?1:0;
    if(state_version)*state_version=result.state_version;
    copy_text(error,error_capacity,result.error);
    return result.committed?1:0;
  } catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
    catch(...){copy_text(error,error_capacity,"UNKNOWN_REVIEWED_CLAIM_VALIDITY_EXCEPTION");return 0;}
}

int surge_question_aspect_get_status(void* opaque, const char* aspect_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity) {
  try {
    if (!opaque || !aspect_id || !*aspect_id) {
      copy_text(error, error_capacity, "C_API_ARGUMENT_INVALID"); return 0;
    }
    const auto& graph = static_cast<Handle*>(opaque)->graph;
    const auto found = graph.nodes().find(aspect_id);
    if (found == graph.nodes().end() || found->second.kind != surge::NodeKind::QuestionAspect) {
      copy_text(error, error_capacity, "QUESTION_ASPECT_NOT_FOUND"); return 0;
    }
    const auto status = graph.question_aspect_status(aspect_id);
    const char* name = status == surge::QuestionAspectStatus::Open ? "OPEN" :
        status == surge::QuestionAspectStatus::Resolved ? "RESOLVED" : "DEFERRED";
    std::ostringstream json;
    json << R"({"aspect_id":)" << json_quote(aspect_id)
         << R"(,"status":)" << json_quote(name)
         << R"(,"state_version":)" << graph.state_version() << '}';
    const auto value = json.str();
    if (required_capacity) *required_capacity = value.size() + 1;
    if (!output || output_capacity < value.size() + 1) {
      copy_text(error, error_capacity, "QUESTION_ASPECT_BUFFER_TOO_SMALL"); return 0;
    }
    copy_text(output, output_capacity, value);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) { copy_text(error, error_capacity, ex.what()); return 0; }
    catch (...) { copy_text(error, error_capacity, "QUESTION_ASPECT_GET_UNKNOWN_EXCEPTION"); return 0; }
}


int surge_question_get_aggregate_status(void* opaque, const char* question_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity) {
  try {
    if (!opaque || !question_id || !*question_id) {
      copy_text(error, error_capacity, "C_API_ARGUMENT_INVALID"); return 0;
    }
    const auto& graph = static_cast<Handle*>(opaque)->graph;
    const auto parent = graph.nodes().find(question_id);
    if (parent == graph.nodes().end() || parent->second.kind != surge::NodeKind::Question) {
      copy_text(error, error_capacity, "QUESTION_NOT_FOUND"); return 0;
    }
    std::vector<std::string> aspect_ids;
    size_t open_count = 0, deferred_count = 0, resolved_count = 0;
    for (const auto& item : graph.nodes()) {
      const auto& node = item.second;
      if (node.kind != surge::NodeKind::QuestionAspect ||
          std::find(node.references.begin(), node.references.end(), question_id) == node.references.end()) continue;
      aspect_ids.push_back(node.id);
      const auto status = graph.question_aspect_status(node.id);
      if (status == surge::QuestionAspectStatus::Open) ++open_count;
      else if (status == surge::QuestionAspectStatus::Deferred) ++deferred_count;
      else ++resolved_count;
    }
    const char* aggregate = aspect_ids.empty() ? "UNKNOWN" :
        open_count ? "OPEN" : deferred_count ? "DEFERRED" : "RESOLVED";
    std::ostringstream json;
    json << R"({"question_id":)" << json_quote(question_id)
         << R"(,"status":)" << json_quote(aggregate)
         << R"(,"counts":{"OPEN":)" << open_count
         << R"(,"DEFERRED":)" << deferred_count
         << R"(,"RESOLVED":)" << resolved_count
         << R"(},"aspect_ids":[)";
    for (size_t i = 0; i < aspect_ids.size(); ++i) {
      if (i) json << ',';
      json << json_quote(aspect_ids[i]);
    }
    json << R"(],"state_version":)" << graph.state_version() << '}';
    const auto value = json.str();
    if (required_capacity) *required_capacity = value.size() + 1;
    if (!output || output_capacity < value.size() + 1) {
      copy_text(error, error_capacity, "QUESTION_AGGREGATE_BUFFER_TOO_SMALL"); return 0;
    }
    copy_text(output, output_capacity, value);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) { copy_text(error, error_capacity, ex.what()); return 0; }
    catch (...) { copy_text(error, error_capacity, "QUESTION_AGGREGATE_GET_UNKNOWN_EXCEPTION"); return 0; }
}



int surge_question_memory_set_state(void*o,const char*id,const char*state,uint64_t expected,char*err,size_t ec){try{if(!o||!id||!*id||!state){copy_text(err,ec,"QUESTION_MEMORY_ARGUMENT_INVALID");return 0;}surge::QuestionMemoryState v;if(!strcmp(state,"ACTIVE"))v=surge::QuestionMemoryState::Active;else if(!strcmp(state,"DORMANT"))v=surge::QuestionMemoryState::Dormant;else if(!strcmp(state,"ARCHIVED"))v=surge::QuestionMemoryState::Archived;else{copy_text(err,ec,"QUESTION_MEMORY_STATE_INVALID");return 0;}std::string detail;bool ok=static_cast<Handle*>(o)->mutations.set_question_memory_state(id,v,expected,detail);copy_text(err,ec,detail);return ok?1:0;}catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"QUESTION_MEMORY_SET_UNKNOWN_EXCEPTION");return 0;}}

int surge_question_memory_record_reference(void* o,const char* id,uint64_t expected,int* reactivated,uint64_t* count,char* err,size_t ec){try{if(!o||!id||!*id){copy_text(err,ec,"QUESTION_ID_REQUIRED");return 0;}bool did=false;std::string detail;auto& g=static_cast<Handle*>(o)->graph;bool ok=static_cast<Handle*>(o)->mutations.record_question_reference(id,expected,did,detail);if(!ok){copy_text(err,ec,detail);return 0;}if(reactivated)*reactivated=did;if(count)*count=g.question_reference_count(id);copy_text(err,ec,"");return 1;}catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"QUESTION_REFERENCE_UNKNOWN_EXCEPTION");return 0;}}
int surge_question_memory_record_reference_with_dependents(void* o,const char* id,uint64_t expected,size_t max_depth,size_t max_nodes,int* reactivated,size_t* dependent_count,char* err,size_t ec){try{if(!o||!id||!*id){copy_text(err,ec,"QUESTION_ID_REQUIRED");return 0;}bool did=false;size_t count=0;std::string detail;bool ok=static_cast<Handle*>(o)->mutations.record_question_reference_with_dependents(id,expected,max_depth,max_nodes,did,count,detail);if(!ok){copy_text(err,ec,detail);return 0;}if(reactivated)*reactivated=did;if(dependent_count)*dependent_count=count;copy_text(err,ec,"");return 1;}catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"QUESTION_DEPENDENCY_REACTIVATION_UNKNOWN_EXCEPTION");return 0;}}
int surge_question_memory_get_state(void* o,const char* id,char* out,size_t cap,size_t* req,char* err,size_t ec){
 try{
  if(!o||!id||!*id){copy_text(err,ec,"QUESTION_ID_REQUIRED");return 0;}
  const auto& g=static_cast<Handle*>(o)->graph;auto n=g.nodes().find(id);
  if(n==g.nodes().end()||n->second.kind!=surge::NodeKind::Question){copy_text(err,ec,"QUESTION_NOT_FOUND");return 0;}
  auto state=g.question_memory_state(id);const char* name=state==surge::QuestionMemoryState::Active?"ACTIVE":state==surge::QuestionMemoryState::Dormant?"DORMANT":"ARCHIVED";
  std::string value="{";
  value+=char(34);value+="question_id";value+=char(34);value+=':';value+=json_quote(id);value+=',';
  value+=char(34);value+="memory_state";value+=char(34);value+=':';value+=json_quote(name);value+=',';
  value+=char(34);value+="state_version";value+=char(34);value+=':';value+=std::to_string(g.state_version());value+='}';
  if(req) *req=value.size()+1;
  if(!out||cap<value.size()+1){copy_text(err,ec,"QUESTION_MEMORY_BUFFER_TOO_SMALL");return 0;}
  copy_text(out,cap,value);copy_text(err,ec,"");return 1;
 }catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"QUESTION_MEMORY_GET_UNKNOWN_EXCEPTION");return 0;}
}
int surge_question_memory_recommendation(double a,double b,double c,double d,double e,double threshold,char*out,size_t cap,size_t*req,char*err,size_t ec){
 try{
  const double factors[]={a,b,c,d,e,threshold};for(double v:factors)if(!std::isfinite(v)||v<0||v>1){copy_text(err,ec,"QUESTION_MEMORY_FACTOR_OUT_OF_RANGE");return 0;}
  const double score=.30*a+.20*b+.25*c+.10*d+.15*e;const char* state=score<threshold?"DORMANT":"ACTIVE";
  std::ostringstream json;json.precision(8);json<<'{'<<char(34)<<"retention_score"<<char(34)<<':'<<score<<','
   <<char(34)<<"recommended_state"<<char(34)<<':'<<json_quote(state)<<','
   <<char(34)<<"weights"<<char(34)<<":{"
   <<char(34)<<"contradiction_strength"<<char(34)<<":0.30,"
   <<char(34)<<"focus_relevance"<<char(34)<<":0.20,"
   <<char(34)<<"unresolved_importance"<<char(34)<<":0.25,"
   <<char(34)<<"recency"<<char(34)<<":0.10,"
   <<char(34)<<"recurrence"<<char(34)<<":0.15}}";
  const auto value=json.str();if(req)*req=value.size()+1;
  if(!out||cap<value.size()+1){copy_text(err,ec,"QUESTION_MEMORY_BUFFER_TOO_SMALL");return 0;}
  copy_text(out,cap,value);copy_text(err,ec,"");return 1;
 }catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"QUESTION_MEMORY_RECOMMENDATION_UNKNOWN_EXCEPTION");return 0;}
}



int surge_question_memory_plan(void* opaque,size_t max_active,char* output,size_t output_capacity,size_t* required,char* error,size_t error_capacity){
 try{
  if(!opaque||max_active>4096){copy_text(error,error_capacity,"QUESTION_MEMORY_CAPACITY_LIMIT");return 0;}
  const auto& graph=static_cast<Handle*>(opaque)->graph;
  struct Item{std::string id;double score,contradiction,focus,unresolved,recency,reference_frequency;bool protected_by_dependency;std::string status;};
  std::vector<Item> items;
  for(const auto& node:graph.nodes()){
   if(node.second.kind!=surge::NodeKind::Question||graph.question_memory_state(node.first)!=surge::QuestionMemoryState::Active)continue;
   std::vector<std::string> aspects;size_t open=0,deferred=0,resolved=0;
   for(const auto& candidate:graph.nodes())if(candidate.second.kind==surge::NodeKind::QuestionAspect&&candidate.second.references.size()==1&&candidate.second.references.front()==node.first){
    aspects.push_back(candidate.first);switch(graph.question_aspect_status(candidate.first)){case surge::QuestionAspectStatus::Open:++open;break;case surge::QuestionAspectStatus::Deferred:++deferred;break;case surge::QuestionAspectStatus::Resolved:++resolved;break;}
   }
   double contradiction=0.0;
   for(const auto& rel:graph.relations())if(rel.second.kind==surge::RelationKind::Contradicts){
    bool linked=node.first.find(rel.first)!=std::string::npos;
    for(const auto& aid:aspects){const auto& text=graph.nodes().at(aid).content;if(text.find(rel.first)!=std::string::npos)linked=true;}
    if(linked)contradiction=1.0;
   }
   double focus=0.0;
   if(graph.focus())for(const auto& candidate:graph.nodes())if(candidate.second.kind==surge::NodeKind::Interest&&candidate.first==graph.focus()->interest_id&&candidate.second.references.size()==2&&std::find(aspects.begin(),aspects.end(),candidate.second.references.front())!=aspects.end()){focus=1.0;break;}
   const size_t total=open+deferred+resolved;
   double unresolved=total?static_cast<double>(open+0.65*deferred+0.15*resolved)/static_cast<double>(total):0.5;
   const double recency=0.5; // Neutral: no wall-clock or timestamp inference.
   bool protected_dependency=false;
   for(const auto& rel:graph.relations())if(rel.second.kind==surge::RelationKind::DependsOn&&(rel.second.from==node.first||rel.second.to==node.first)){protected_dependency=true;break;}
   double reference_frequency=std::min(1.0,static_cast<double>(graph.question_reference_count(node.first))/5.0);
   double score=.30*contradiction+.20*focus+.25*unresolved+.10*recency+.15*reference_frequency;
   if(protected_dependency)score=std::max(score,.75);
   items.push_back({node.first,score,contradiction,focus,unresolved,recency,reference_frequency,protected_dependency,total?(open?"OPEN":(deferred?"DEFERRED":"RESOLVED")):"UNKNOWN"});
  }
  std::sort(items.begin(),items.end(),[](const Item&a,const Item&b){return a.score!=b.score?a.score>b.score:a.id<b.id;});
  auto quote=[](const std::string& value){return json_quote(value);};
  std::ostringstream json;json.precision(8);
  json<<"{\"state_version\":"<<graph.state_version()<<",\"capacity\":"<<max_active<<",\"active_count\":"<<items.size()<<",\"demotion_candidates\":[";
  for(size_t i=max_active;i<items.size();++i){if(i>max_active)json<<',';json<<quote(items[i].id);}
  json<<"],\"scores\":[";
  for(size_t i=0;i<items.size();++i){if(i)json<<',';const auto& x=items[i];
   json<<"{\"question_id\":"<<quote(x.id)<<",\"retention_score\":"<<x.score<<",\"recommended_state\":"<<quote(i<max_active?"ACTIVE":"DORMANT")
       <<",\"aspect_state\":"<<quote(x.status)<<",\"protected_by_dependency\":"<<(x.protected_by_dependency?"true":"false")
       <<",\"factors\":{\"contradiction_strength\":"<<x.contradiction<<",\"focus_relevance\":"<<x.focus
       <<",\"unresolved_importance\":"<<x.unresolved<<",\"recency\":"<<x.recency<<",\"reference_frequency\":"<<std::min(1.0,static_cast<double>(graph.question_reference_count(x.id))/5.0)<<"}}";
  }
  json<<"]}";
  const auto value=json.str();if(required)*required=value.size()+1;
  if(!output||output_capacity<value.size()+1){copy_text(error,error_capacity,"QUESTION_MEMORY_PLAN_BUFFER_TOO_SMALL");return 0;}
  copy_text(output,output_capacity,value);copy_text(error,error_capacity,"");return 1;
 }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}catch(...){copy_text(error,error_capacity,"QUESTION_MEMORY_PLAN_UNKNOWN_EXCEPTION");return 0;}
}

int surge_question_memory_enforce_capacity(void* opaque,const char* const* ids,const double* scores,size_t count,size_t max_active,uint64_t expected,char* output,size_t output_capacity,size_t* required,char* error,size_t error_capacity){
 try{
  if(!opaque||count>4096||max_active>4096||(count&&(!ids||!scores))){copy_text(error,error_capacity,"QUESTION_MEMORY_CAPACITY_ARGUMENT_INVALID");return 0;}
  size_t upper=128;
  for(size_t i=0;i<count;++i){
   if(!ids[i]||!*ids[i]){copy_text(error,error_capacity,"QUESTION_MEMORY_SCORE_INVALID");return 0;}
   const size_t length=std::strlen(ids[i]);if(length>4096||upper>static_cast<size_t>(-1)-6*length-16){copy_text(error,error_capacity,"QUESTION_MEMORY_CAPACITY_ARGUMENT_INVALID");return 0;}
   upper+=6*length+16;
  }
  if(required)*required=upper;
  if(!output||output_capacity<upper){copy_text(error,error_capacity,"QUESTION_MEMORY_BUFFER_TOO_SMALL");return 0;}
  std::vector<std::pair<std::string,double>> values;values.reserve(count);
  for(size_t i=0;i<count;++i)values.emplace_back(ids[i],scores[i]);
  auto& graph=static_cast<Handle*>(opaque)->graph;std::vector<std::string> demoted;std::string detail;
  if(!static_cast<Handle*>(opaque)->mutations.enforce_question_memory_capacity(values,max_active,expected,demoted,detail)){copy_text(error,error_capacity,detail);return 0;}
  size_t active_after=0;for(const auto& n:graph.nodes())if(n.second.kind==surge::NodeKind::Question&&graph.question_memory_state(n.first)==surge::QuestionMemoryState::Active)++active_after;
  std::ostringstream json;json<<'{'<<char(34)<<"capacity"<<char(34)<<':'<<max_active<<','<<char(34)<<"active_after"<<char(34)<<':'<<active_after<<','<<char(34)<<"demoted"<<char(34)<<":[";
  for(size_t i=0;i<demoted.size();++i){if(i)json<<',';json<<json_quote(demoted[i]);}
  json<<"],"<<char(34)<<"state_version"<<char(34)<<':'<<graph.state_version()<<'}';
  const auto value=json.str();if(required)*required=value.size()+1;
  copy_text(output,output_capacity,value);copy_text(error,error_capacity,"");return 1;
 }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}catch(...){copy_text(error,error_capacity,"QUESTION_MEMORY_CAPACITY_UNKNOWN_EXCEPTION");return 0;}
}
int surge_question_get_context_bundle(void* opaque,const char* question_id,size_t max_depth,size_t max_nodes,char* output,size_t output_capacity,size_t* required_capacity,char* error,size_t error_capacity){
 try{
  if(!opaque||!question_id||!*question_id||max_depth==0||max_depth>64||max_nodes==0||max_nodes>4096){copy_text(error,error_capacity,"QUESTION_CONTEXT_ARGUMENT_INVALID");return 0;}
  const auto& graph=static_cast<Handle*>(opaque)->graph;const auto root=graph.nodes().find(question_id);
  if(root==graph.nodes().end()||root->second.kind!=surge::NodeKind::Question){copy_text(error,error_capacity,"QUESTION_NOT_FOUND");return 0;}
  std::set<std::string> questions{question_id};std::map<std::string,std::vector<std::string>> reverse;
  for(const auto& item:graph.relations()){const auto&r=item.second;if(r.kind!=surge::RelationKind::DependsOn)continue;auto a=graph.nodes().find(r.from),b=graph.nodes().find(r.to);if(a!=graph.nodes().end()&&b!=graph.nodes().end()&&a->second.kind==surge::NodeKind::Question&&b->second.kind==surge::NodeKind::Question)reverse[r.to].push_back(r.from);}
  for(auto& item:reverse)std::sort(item.second.begin(),item.second.end());
  std::vector<std::pair<std::string,size_t>> queue{{question_id,0}};
  for(size_t i=0;i<queue.size();++i){auto cur=queue[i];if(cur.second>=max_depth)continue;for(const auto& next:reverse[cur.first])if(questions.insert(next).second){if(questions.size()>max_nodes){copy_text(error,error_capacity,"QUESTION_CONTEXT_NODE_LIMIT");return 0;}queue.push_back({next,cur.second+1});}}
  std::set<std::string> aspects,claims,evidence;
  for(const auto& item:graph.nodes()){const auto& n=item.second;if(n.kind==surge::NodeKind::QuestionAspect)for(const auto& ref:n.references)if(questions.count(ref)){aspects.insert(n.id);break;}}
  std::set<std::string> anchors=questions;anchors.insert(aspects.begin(),aspects.end());std::set<std::string> about_targets;
  for(const auto& item:graph.relations()){const auto&r=item.second;if(r.kind==surge::RelationKind::About&&anchors.count(r.from)){auto target=graph.nodes().find(r.to);if(target!=graph.nodes().end()&&(target->second.kind==surge::NodeKind::Cluster||target->second.kind==surge::NodeKind::Entity))about_targets.insert(r.to);}}
  for(const auto& item:graph.nodes()){const auto& n=item.second;if(n.kind!=surge::NodeKind::Claim&&n.kind!=surge::NodeKind::Evidence)continue;bool linked=false;for(const auto& ref:n.references)if(anchors.count(ref)||about_targets.count(ref)){linked=true;break;}if(!linked)for(const auto& rel:graph.relations()){const auto&r=rel.second;if((r.from==n.id&&(anchors.count(r.to)||about_targets.count(r.to)))||(r.to==n.id&&(anchors.count(r.from)||about_targets.count(r.from)))){linked=true;break;}}if(linked){if(n.kind==surge::NodeKind::Claim)claims.insert(n.id);else evidence.insert(n.id);}}
  for(const auto& item:graph.relations()){const auto&r=item.second;auto a=graph.nodes().find(r.from),b=graph.nodes().find(r.to);if(claims.count(r.from)&&b!=graph.nodes().end()&&b->second.kind==surge::NodeKind::Evidence)evidence.insert(r.to);if(claims.count(r.to)&&a!=graph.nodes().end()&&a->second.kind==surge::NodeKind::Evidence)evidence.insert(r.from);}
  if(questions.size()+aspects.size()+claims.size()+evidence.size()>max_nodes){copy_text(error,error_capacity,"QUESTION_CONTEXT_NODE_LIMIT");return 0;}
  auto kn=[](surge::NodeKind k){switch(k){case surge::NodeKind::Question:return "QUESTION";case surge::NodeKind::QuestionAspect:return "QUESTION_ASPECT";case surge::NodeKind::Cluster:return "CLUSTER";case surge::NodeKind::Entity:return "ENTITY";case surge::NodeKind::Claim:return "CLAIM";case surge::NodeKind::Evidence:return "EVIDENCE";case surge::NodeKind::ClusterLifecycle:return "CLUSTER_LIFECYCLE";default:return "UNKNOWN";}};
  auto mn=[](surge::QuestionMemoryState s){return s==surge::QuestionMemoryState::Active?"ACTIVE":s==surge::QuestionMemoryState::Dormant?"DORMANT":"ARCHIVED";};
  auto an=[](surge::QuestionAspectStatus s){return s==surge::QuestionAspectStatus::Open?"OPEN":s==surge::QuestionAspectStatus::Resolved?"RESOLVED":"DEFERRED";};
  auto cn=[](surge::ClaimStatus s){return s==surge::ClaimStatus::Active?"ACTIVE":s==surge::ClaimStatus::Superseded?"SUPERSEDED":"RETRACTED";};
  std::set<std::string> included=questions;included.insert(aspects.begin(),aspects.end());included.insert(about_targets.begin(),about_targets.end());included.insert(claims.begin(),claims.end());included.insert(evidence.begin(),evidence.end());
  std::ostringstream json;json<<"{\"question_id\":"<<json_quote(question_id)<<",\"state_version\":"<<graph.state_version()<<",\"nodes\":[";bool first=true;
  for(const auto&id:included){const auto&n=graph.nodes().at(id);if(!first)json<<',';first=false;json<<"{\"id\":"<<json_quote(n.id)<<",\"kind\":"<<json_quote(kn(n.kind))<<",\"content\":"<<json_quote(n.content)<<",\"references\":[";for(size_t i=0;i<n.references.size();++i){if(i)json<<',';json<<json_quote(n.references[i]);}json<<']';if(n.kind==surge::NodeKind::Question)json<<",\"memory_state\":"<<json_quote(mn(graph.question_memory_state(n.id)));if(n.kind==surge::NodeKind::QuestionAspect)json<<",\"aspect_status\":"<<json_quote(an(graph.question_aspect_status(n.id)));if(n.kind==surge::NodeKind::Claim)json<<",\"claim_status\":"<<json_quote(cn(graph.claim_status(n.id)));json<<'}';}
  json<<"],\"relations\":[";first=true;for(const auto&item:graph.relations()){const auto&r=item.second;if(!included.count(r.from)||!included.count(r.to))continue;if(!first)json<<',';first=false;const char* kind=r.kind==surge::RelationKind::Supports?"SUPPORTS":r.kind==surge::RelationKind::Contradicts?"CONTRADICTS":r.kind==surge::RelationKind::AgreesWith?"AGREES_WITH":r.kind==surge::RelationKind::ConflictsWith?"CONFLICTS_WITH":r.kind==surge::RelationKind::DependsOn?"DEPENDS_ON":r.kind==surge::RelationKind::About?"ABOUT":r.kind==surge::RelationKind::Supersedes?"SUPERSEDES":r.kind==surge::RelationKind::Retracts?"RETRACTS":r.kind==surge::RelationKind::Duplicates?"DUPLICATES":"CONTAINS";json<<"{\"id\":"<<json_quote(r.id)<<",\"kind\":"<<json_quote(kind)<<",\"from\":"<<json_quote(r.from)<<",\"to\":"<<json_quote(r.to)<<'}';}
  json<<"]}";auto value=json.str();if(required_capacity)*required_capacity=value.size()+1;if(!output||output_capacity<value.size()+1){copy_text(error,error_capacity,"QUESTION_CONTEXT_BUFFER_TOO_SMALL");return 0;}copy_text(output,output_capacity,value);copy_text(error,error_capacity,"");return 1;
 }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}catch(...){copy_text(error,error_capacity,"QUESTION_CONTEXT_UNKNOWN_EXCEPTION");return 0;}
}

int surge_question_get_reevaluation_plan(void* opaque,const char* question_id,size_t max_depth,size_t max_nodes,char* output,size_t output_capacity,size_t* required_capacity,char* error,size_t error_capacity){
 try{
  if(!opaque||!question_id||!*question_id||max_depth==0||max_depth>64||max_nodes==0||max_nodes>4096){copy_text(error,error_capacity,"QUESTION_REEVALUATION_ARGUMENT_INVALID");return 0;}
  const auto& graph=static_cast<Handle*>(opaque)->graph;
  auto root=graph.nodes().find(question_id);
  if(root==graph.nodes().end()||root->second.kind!=surge::NodeKind::Question){copy_text(error,error_capacity,"QUESTION_NOT_FOUND");return 0;}
  std::map<std::string,std::vector<std::string>> reverse;
  for(const auto& item:graph.relations()){const auto&r=item.second;if(r.kind!=surge::RelationKind::DependsOn)continue;auto a=graph.nodes().find(r.from),b=graph.nodes().find(r.to);if(a!=graph.nodes().end()&&b!=graph.nodes().end()&&a->second.kind==surge::NodeKind::Question&&b->second.kind==surge::NodeKind::Question)reverse[r.to].push_back(r.from);}
  for(auto& item:reverse)std::sort(item.second.begin(),item.second.end());
  std::set<std::string> questions{question_id};std::vector<std::pair<std::string,size_t>> queue{{question_id,0}};
  for(size_t i=0;i<queue.size();++i){auto cur=queue[i];if(cur.second>=max_depth)continue;for(const auto& next:reverse[cur.first])if(questions.insert(next).second){queue.push_back({next,cur.second+1});if(questions.size()>max_nodes){copy_text(error,error_capacity,"QUESTION_REEVALUATION_NODE_LIMIT");return 0;}}}
  std::vector<const surge::GraphNode*> aspects;
  for(const auto& item:graph.nodes()){const auto&n=item.second;if(n.kind!=surge::NodeKind::QuestionAspect)continue;for(const auto&ref:n.references)if(questions.count(ref)){aspects.push_back(&n);break;}}
  std::sort(aspects.begin(),aspects.end(),[](auto*a,auto*b){return a->id<b->id;});
  std::set<std::string> included=questions;
  std::ostringstream rows;bool first=true;
  auto status_name=[](surge::QuestionAspectStatus s){return s==surge::QuestionAspectStatus::Open?"OPEN":s==surge::QuestionAspectStatus::Resolved?"RESOLVED":"DEFERRED";};
  for(const auto* aspect:aspects){
   included.insert(aspect->id);
   std::set<std::string> anchors{aspect->id};for(const auto&ref:aspect->references)if(questions.count(ref))anchors.insert(ref);
   std::set<std::string> clusters;
   for(const auto&item:graph.relations()){const auto&r=item.second;if(r.kind==surge::RelationKind::About&&anchors.count(r.from)){auto n=graph.nodes().find(r.to);if(n!=graph.nodes().end()&&(n->second.kind==surge::NodeKind::Cluster||n->second.kind==surge::NodeKind::Entity))clusters.insert(r.to);}}
   for(const auto& cluster:clusters) included.insert(cluster);
   std::set<std::string> claims;
   for(const auto&item:graph.relations()){const auto&r=item.second;if(r.kind==surge::RelationKind::About&&clusters.count(r.to)){auto n=graph.nodes().find(r.from);if(n!=graph.nodes().end()&&n->second.kind==surge::NodeKind::Claim)claims.insert(r.from);}}
   for(const auto&claim:claims)included.insert(claim);
   size_t supporting=0,contradicting=0;std::set<std::string> support_evidence,conflict_refs,retracted_claims,conflict_relation_ids;
   for(const auto&item:graph.relations()){
    const auto&r=item.second;
    if(r.kind==surge::RelationKind::Supports){
     std::string evidence_id,claim_id;
     auto from=graph.nodes().find(r.from),to=graph.nodes().find(r.to);
     if(from!=graph.nodes().end()&&to!=graph.nodes().end()&&from->second.kind==surge::NodeKind::Evidence&&to->second.kind==surge::NodeKind::Claim){evidence_id=r.from;claim_id=r.to;}
     else if(from!=graph.nodes().end()&&to!=graph.nodes().end()&&from->second.kind==surge::NodeKind::Claim&&to->second.kind==surge::NodeKind::Evidence){claim_id=r.from;evidence_id=r.to;}
     if(claims.count(claim_id)&&graph.claim_status(claim_id)==surge::ClaimStatus::Active){++supporting;support_evidence.insert(evidence_id);}
    }
    if(r.kind==surge::RelationKind::Contradicts||r.kind==surge::RelationKind::ConflictsWith||r.kind==surge::RelationKind::Retracts){
     bool touches_claim=claims.count(r.from)||claims.count(r.to);
     if(touches_claim){++contradicting;conflict_refs.insert(r.from);conflict_refs.insert(r.to);conflict_relation_ids.insert(r.id);if(r.kind==surge::RelationKind::Retracts&&claims.count(r.to))retracted_claims.insert(r.to);}
    }
   }
   for(const auto&id:support_evidence)if(!id.empty())included.insert(id);
   for(const auto&id:conflict_refs)if(graph.nodes().count(id))included.insert(id);
   auto current=graph.question_aspect_status(aspect->id);
   std::string proposed=status_name(current),reason="INSUFFICIENT_EVIDENCE";
   if(!retracted_claims.empty()){proposed="OPEN";reason="RETRACTED_EVIDENCE";}
   else if(contradicting>0){proposed="OPEN";reason="CONTRADICTORY_EVIDENCE";}
   else if(supporting>0){proposed="RESOLVED";reason="SUPPORTED_EVIDENCE";}
   if(!first) rows<<',';
   first=false;
   rows<<"{\"aspect_id\":"<<json_quote(aspect->id)<<",\"question_ids\":[";
   bool qfirst=true;for(const auto&ref:aspect->references)if(questions.count(ref)){if(!qfirst)rows<<',';qfirst=false;rows<<json_quote(ref);}
   rows<<"],\"current_status\":"<<json_quote(status_name(current))<<",\"proposed_status\":"<<json_quote(proposed)<<",\"reason\":"<<json_quote(reason)<<",\"supporting_evidence_count\":"<<supporting;
   rows<<",\"contradicting_relation_count\":"<<contradicting<<",\"supporting_evidence_ids\":[";
   bool efirst=true;for(const auto&id:support_evidence){if(!efirst)rows<<',';efirst=false;rows<<json_quote(id);}
   rows<<"],\"retracted_claim_ids\":[";
   bool rfirst=true;for(const auto&id:retracted_claims){if(!rfirst)rows<<',';rfirst=false;rows<<json_quote(id);}
   rows<<"],\"conflict_relation_ids\":[";
   bool xfirst=true;for(const auto&id:conflict_relation_ids){if(!xfirst)rows<<',';xfirst=false;rows<<json_quote(id);}
   rows<<"],\"claim_ids\":[";
   bool cfirst=true;for(const auto&id:claims){if(!cfirst)rows<<',';cfirst=false;rows<<json_quote(id);}rows<<"]}";
  }
  if(included.size()>max_nodes){copy_text(error,error_capacity,"QUESTION_REEVALUATION_NODE_LIMIT");return 0;}
  std::ostringstream json;json<<"{\"question_id\":"<<json_quote(question_id)<<",\"state_version\":"<<graph.state_version()<<",\"question_ids\":[";
  bool qfirst=true;for(const auto&id:questions){if(!qfirst)json<<',';qfirst=false;json<<json_quote(id);}
  json<<"],\"aspect_proposals\":["<<rows.str()<<"],\"node_count\":"<<included.size()<<",\"read_only\":true}";
  const auto value=json.str();if(required_capacity)*required_capacity=value.size()+1;
  if(!output||output_capacity<value.size()+1){copy_text(error,error_capacity,"QUESTION_REEVALUATION_PLAN_BUFFER_TOO_SMALL");return 0;}
  copy_text(output,output_capacity,value);copy_text(error,error_capacity,"");return 1;
 }catch(const std::exception&ex){copy_text(error,error_capacity,ex.what());return 0;}catch(...){copy_text(error,error_capacity,"QUESTION_REEVALUATION_PLAN_UNKNOWN_EXCEPTION");return 0;}
}

int surge_question_get_reevaluation_candidates(void* opaque, const char* question_id,
    size_t max_depth, size_t max_nodes, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity) {
  try {
    if (!opaque || !question_id || !*question_id || max_depth == 0 ||
        max_depth > 64 || max_nodes == 0 || max_nodes > 4096) {
      copy_text(error, error_capacity, "QUESTION_REEVALUATION_ARGUMENT_INVALID");
      return 0;
    }
    const auto& graph = static_cast<Handle*>(opaque)->graph;
    const auto root = graph.nodes().find(question_id);
    if (root == graph.nodes().end() || root->second.kind != surge::NodeKind::Question) {
      copy_text(error, error_capacity, "QUESTION_NOT_FOUND");
      return 0;
    }
    struct Edge { std::string dependent_id, relation_id; };
    struct Step { std::string question_id; size_t depth; std::vector<std::string> path; };
    struct Candidate { std::string question_id, status; size_t depth; std::vector<std::string> path; };
    std::map<std::string, std::vector<Edge>> reverse_dependencies;
    for (const auto& item : graph.relations()) {
      const auto& relation = item.second;
      if (relation.kind != surge::RelationKind::DependsOn) continue;
      const auto from = graph.nodes().find(relation.from);
      const auto to = graph.nodes().find(relation.to);
      if (from == graph.nodes().end() || to == graph.nodes().end() ||
          from->second.kind != surge::NodeKind::Question ||
          to->second.kind != surge::NodeKind::Question) continue;
      reverse_dependencies[relation.to].push_back({relation.from, relation.id});
    }
    std::vector<Step> pending{{question_id, 0, {}}};
    size_t cursor = 0;
    std::set<std::string> visited{question_id};
    std::vector<Candidate> candidates;
    while (cursor < pending.size()) {
      const Step current = pending[cursor++];
      if (current.depth >= max_depth) continue;
      const auto edges = reverse_dependencies.find(current.question_id);
      if (edges == reverse_dependencies.end()) continue;
      for (const auto& edge : edges->second) {
        if (!visited.insert(edge.dependent_id).second) continue;
        if (candidates.size() >= max_nodes) {
          copy_text(error, error_capacity, "QUESTION_REEVALUATION_NODE_LIMIT");
          return 0;
        }
        Step next{edge.dependent_id, current.depth + 1, current.path};
        next.path.push_back(edge.relation_id);
        pending.push_back(next);
        size_t open_count = 0, deferred_count = 0, aspect_count = 0;
        for (const auto& node_item : graph.nodes()) {
          const auto& node = node_item.second;
          if (node.kind != surge::NodeKind::QuestionAspect ||
              std::find(node.references.begin(), node.references.end(), edge.dependent_id) == node.references.end()) continue;
          ++aspect_count;
          const auto status = graph.question_aspect_status(node.id);
          if (status == surge::QuestionAspectStatus::Open) ++open_count;
          else if (status == surge::QuestionAspectStatus::Deferred) ++deferred_count;
        }
        const std::string aggregate = aspect_count == 0 ? "UNKNOWN" :
            open_count ? "OPEN" : deferred_count ? "DEFERRED" : "RESOLVED";
        candidates.push_back({edge.dependent_id, aggregate, next.depth, next.path});
      }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
      if (a.depth != b.depth) return a.depth < b.depth;
      return a.question_id < b.question_id;
    });
    std::ostringstream json;
    json << "{\"trigger_question_id\":" << json_quote(question_id)
         << ",\"max_depth\":" << max_depth << ",\"max_nodes\":" << max_nodes
         << ",\"candidates\":[";
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (i) json << ',';
      const auto& item = candidates[i];
      json << "{\"question_id\":" << json_quote(item.question_id)
           << ",\"status\":" << json_quote(item.status)
           << ",\"depth\":" << item.depth << ",\"via_relation_ids\":[";
      for (size_t j = 0; j < item.path.size(); ++j) {
        if (j) json << ',';
        json << json_quote(item.path[j]);
      }
      json << "]}";
    }
    json << "],\"state_version\":" << graph.state_version() << '}';
    const std::string value = json.str();
    if (required_capacity) *required_capacity = value.size() + 1;
    if (!output || output_capacity < value.size() + 1) {
      copy_text(error, error_capacity, "QUESTION_REEVALUATION_BUFFER_TOO_SMALL");
      return 0;
    }
    copy_text(output, output_capacity, value);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) {
    copy_text(error, error_capacity, ex.what()); return 0;
  } catch (...) {
    copy_text(error, error_capacity, "QUESTION_REEVALUATION_UNKNOWN_EXCEPTION"); return 0;
  }
}

int surge_question_route_contradiction(void* opaque, const char* relation_id,
    const char* policy_version, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity) {
  try {
    if (!opaque || !relation_id || !policy_version) {
      copy_text(error, error_capacity, "C_API_ARGUMENT_INVALID"); return 0;
    }
    auto& graph = static_cast<Handle*>(opaque)->graph;
    const auto routed = surge::QuestionRouter::route(graph, relation_id, policy_version);
    std::ostringstream json;
    json << R"({"committed":)" << (routed.committed ? "true" : "false")
         << R"(,"duplicate":)" << (routed.duplicate ? "true" : "false")
         << R"(,"focus_selected":)" << (routed.focus_selected ? "true" : "false")
         << R"(,"state_version":)" << routed.state_version
         << R"(,"question_id":)" << json_quote(routed.question_id)
         << R"(,"question_aspect_id":)" << json_quote(routed.question_aspect_id)
         << R"(,"interest_ids":[)";
    for (size_t i = 0; i < routed.interest_ids.size(); ++i) {
      if (i) json << ',';
      json << json_quote(routed.interest_ids[i]);
    }
    json << R"(],"error":)" << json_quote(routed.error) << '}';
    const auto value = json.str();
    if (required_capacity) *required_capacity = value.size() + 1;
    if (!output || output_capacity < value.size() + 1) {
      copy_text(error, error_capacity, "QUESTION_ROUTE_BUFFER_TOO_SMALL"); return 0;
    }
    copy_text(output, output_capacity, value);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) { copy_text(error, error_capacity, ex.what()); return 0; }
    catch (...) { copy_text(error, error_capacity, "QUESTION_ROUTE_UNKNOWN_EXCEPTION"); return 0; }
}

int surge_question_route_contradiction_scoped(void* opaque, const char* relation_id,
    const char* policy_version, const char* scope_key, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity) {
  try {
    if (!opaque || !relation_id || !policy_version || !scope_key) { copy_text(error, error_capacity, "C_API_ARGUMENT_INVALID"); return 0; }
    auto& graph = static_cast<Handle*>(opaque)->graph;
    const auto routed = surge::QuestionRouter::route_scoped(graph, relation_id, policy_version, scope_key);
    std::ostringstream json;
    json << R"({"committed":)" << (routed.committed ? "true" : "false")
         << R"(,"duplicate":)" << (routed.duplicate ? "true" : "false")
         << R"(,"focus_selected":)" << (routed.focus_selected ? "true" : "false")
         << R"(,"state_version":)" << routed.state_version
         << R"(,"question_id":)" << json_quote(routed.question_id)
         << R"(,"question_aspect_id":)" << json_quote(routed.question_aspect_id)
         << R"(,"interest_ids":[)";
    for (size_t i = 0; i < routed.interest_ids.size(); ++i) { if (i) json << ','; json << json_quote(routed.interest_ids[i]); }
    json << R"(],"error":)" << json_quote(routed.error) << '}';
    const auto value = json.str(); if (required_capacity) *required_capacity = value.size() + 1;
    if (!output || output_capacity < value.size() + 1) { copy_text(error, error_capacity, "QUESTION_ROUTE_BUFFER_TOO_SMALL"); return 0; }
    copy_text(output, output_capacity, value); copy_text(error, error_capacity, ""); return 1;
  } catch (const std::exception& ex) { copy_text(error, error_capacity, ex.what()); return 0; }
    catch (...) { copy_text(error, error_capacity, "QUESTION_ROUTE_UNKNOWN_EXCEPTION"); return 0; }
}

int surge_question_aspect_set_status_batch(void* opaque, const char* const* aspect_ids,
    const char* const* statuses, size_t count, uint64_t expected_state_version,
    uint64_t* state_version, char* error, size_t error_capacity) {
  try {
    if (!opaque || !aspect_ids || !statuses || count == 0 || count > 4096) {
      copy_text(error, error_capacity, "QUESTION_ASPECT_BATCH_ARGUMENT_INVALID"); return 0;
    }
    std::vector<std::pair<std::string, surge::QuestionAspectStatus>> updates;
    updates.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      if (!aspect_ids[i] || !*aspect_ids[i] || !statuses[i]) {
        copy_text(error, error_capacity, "QUESTION_ASPECT_BATCH_ARGUMENT_INVALID"); return 0;
      }
      const std::string status(statuses[i]);
      surge::QuestionAspectStatus value;
      if (status == "OPEN") value = surge::QuestionAspectStatus::Open;
      else if (status == "RESOLVED") value = surge::QuestionAspectStatus::Resolved;
      else if (status == "DEFERRED") value = surge::QuestionAspectStatus::Deferred;
      else { copy_text(error, error_capacity, "QUESTION_ASPECT_STATUS_INVALID"); return 0; }
      updates.emplace_back(aspect_ids[i], value);
    }
    auto& graph = static_cast<Handle*>(opaque)->graph;
    std::string message;
    const bool ok = static_cast<Handle*>(opaque)->mutations.apply_question_aspect_statuses(updates, expected_state_version, message);
    copy_text(error, error_capacity, message);
    if (!ok) return 0;
    if (state_version) *state_version = graph.state_version();
    return 1;
  } catch (const std::exception& ex) { copy_text(error, error_capacity, ex.what()); return 0; }
    catch (...) { copy_text(error, error_capacity, "QUESTION_ASPECT_BATCH_UNKNOWN_EXCEPTION"); return 0; }
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
    const bool ok = static_cast<Handle*>(opaque)->mutations.set_question_aspect_status(aspect_id, value, expected_state_version, message);
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

int surge_context_graph_claim_validity_at(void* opaque,const char* claim_id,
    const char* instant,char* output,size_t output_capacity,
    char* error,size_t error_capacity) {
  try {
    if(!opaque||!claim_id||!instant||!output||output_capacity==0){
      copy_text(error,error_capacity,"CLAIM_VALIDITY_QUERY_ARGUMENT_INVALID");return 0;
    }
    const auto status=static_cast<Handle*>(opaque)->graph.claim_validity_at(claim_id,instant);
    const char* value=status==surge::ClaimTemporalState::Unknown?"UNKNOWN":
        status==surge::ClaimTemporalState::NotYetValid?"NOT_YET_VALID":
        status==surge::ClaimTemporalState::Valid?"VALID":"EXPIRED";
    copy_text(output,output_capacity,value);copy_text(error,error_capacity,"");return 1;
  } catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
    catch(...){copy_text(error,error_capacity,"UNKNOWN_CLAIM_VALIDITY_QUERY_EXCEPTION");return 0;}
}

int surge_context_graph_retrieve_page(void* opaque, const char* query, const char* kinds_csv,
    size_t offset, size_t limit, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity) {
  try {
    if (!opaque || !query || !*query || offset > 10000000 || limit == 0 || limit > 1000) {
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
      if (total > 0 && hits > 0)
        return hits == total ? 65 + hits : 20 + (40 * hits / total);

      // Japanese has no whitespace-delimited word boundaries. Fall back to
      // UTF-8 code-point bigram overlap instead of treating the whole query
      // as one indivisible token.
      const bool has_non_ascii = std::any_of(needle.begin(), needle.end(),
          [](unsigned char ch) { return ch >= 0x80; });
      if (!has_non_ascii) return 0;
      auto codepoints = [](const std::string& text) {
        std::vector<std::string> points;
        for (std::size_t i = 0; i < text.size();) {
          const unsigned char lead = static_cast<unsigned char>(text[i]);
          std::size_t width = lead < 0x80 ? 1 :
              (lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC2 ? 2 : 1);
          if (i + width > text.size()) width = 1;
          points.push_back(text.substr(i, width));
          i += width;
        }
        return points;
      };
      const auto query_points = codepoints(needle);
      const auto hay_points = codepoints(hay);
      auto make_grams = [](const std::vector<std::string>& points) {
        std::set<std::string> grams;
        if (points.size() == 1) grams.insert(points.front());
        else for (std::size_t i = 0; i + 1 < points.size(); ++i)
          grams.insert(points[i] + points[i + 1]);
        return grams;
      };
      const auto query_grams = make_grams(query_points);
      const auto hay_grams = make_grams(hay_points);
      if (query_grams.empty() || hay_grams.empty()) return 0;
      std::size_t overlap = 0;
      for (const auto& gram : query_grams)
        if (hay_grams.count(gram)) ++overlap;
      if (overlap == 0) return 0;
      return 25 + static_cast<int>(35 * overlap / query_grams.size());
    };
    auto quote = [](const std::string& value) {
      std::string out = "\"";
      for (unsigned char ch : value) {
        switch (ch) {
          case '"': out += "\\\""; break;
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
        case surge::NodeKind::ChaosCluster: return "CHAOS_CLUSTER";
        case surge::NodeKind::Entity: return "ENTITY";
        case surge::NodeKind::Evidence: return "EVIDENCE";
        case surge::NodeKind::Claim: return "CLAIM";
        case surge::NodeKind::Question: return "QUESTION";
        case surge::NodeKind::QuestionAspect: return "QUESTION_ASPECT";
        case surge::NodeKind::Dependency: return "DEPENDENCY";
        case surge::NodeKind::Interest: return "INTEREST";
        case surge::NodeKind::ClusterLifecycle: return "CLUSTER_LIFECYCLE";
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
        case surge::RelationKind::Supersedes: return "SUPERSEDES";
        case surge::RelationKind::Retracts: return "RETRACTS";
        case surge::RelationKind::Duplicates: return "DUPLICATES";
        case surge::RelationKind::Contains: return "CONTAINS";
        case surge::RelationKind::DerivedFrom: return "DERIVED_FROM";
      }
      return "UNKNOWN";
    };
    for (const auto& entry : graph.nodes()) {
      const auto& node = entry.second;
      // Dormant and archived Questions remain durable but are hidden from ordinary retrieval.
      if (node.kind == surge::NodeKind::Question &&
          graph.question_memory_state(node.id) != surge::QuestionMemoryState::Active) continue;
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
      if (node.kind == surge::NodeKind::Claim) {
        const auto status = graph.claim_status(node.id);
        const char* status_text = status == surge::ClaimStatus::Active ? "ACTIVE" :
            status == surge::ClaimStatus::Superseded ? "SUPERSEDED" : "RETRACTED";
        item << "\"claim_status\":" << quote(status_text) << ",";
        const auto validity = graph.latest_claim_validity(node.id);
        if (validity) {
          const auto& value = validity->validity;
          item << "\"claim_validity\":{\"revision\":" << value.revision
               << ",\"valid_from\":" << quote(value.valid_from)
               << ",\"valid_until\":";
          if (value.valid_until.empty()) item << "null";
          else item << quote(value.valid_until);
          item << ",\"basis_evidence_id\":" << quote(value.basis_evidence_id)
               << ",\"recorded_state_version\":" << validity->recorded_state_version
                << ",\"review_evidence_id\":" << quote(value.review_evidence_id)
                << ",\"reviewer_id\":" << quote(value.reviewer_id) << "},";
        } else item << "\"claim_validity\":null,";
      }
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
    const size_t begin = std::min(offset, hits.size());
    const size_t count = std::min(limit, hits.size() - begin);
    for (size_t i = 0; i < count; ++i) {
      if (i) result << ',';
      result << hits[begin + i].json;
    }
    result << "],\"offset\":" << offset << ",\"next_offset\":";
    if (begin + count < hits.size()) result << (begin + count);
    else result << "null";
    result << ",\"total_matches\":" << hits.size() << "}";
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


int surge_context_graph_retrieve(void* opaque, const char* query, const char* kinds_csv,
    size_t limit, char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity) {
  return surge_context_graph_retrieve_page(opaque, query, kinds_csv, 0, limit,
      output, output_capacity, required_capacity, error, error_capacity);
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
      for (unsigned char ch : value) {
        switch (ch) {
          case '"': out += "\\\""; break;
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
    const auto& record = found->second;
    std::ostringstream json;
    json << "{\"delta_id\":" << quote(record.delta_id) << ",\"triggered\":[";
    for (size_t i = 0; i < record.triggered.size(); ++i) {
      if (i) json << ',';
      const auto& item = record.triggered[i];
      json << "{\"dependency_id\":" << quote(item.dependency_id)
           << ",\"prerequisite_id\":" << quote(item.prerequisite_id)
           << ",\"dependent_id\":" << quote(item.dependent_id) << "}";
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
    json << "]}";
    const std::string payload = json.str();
    if (required_capacity) *required_capacity = payload.size() + 1;
    if (!output || output_capacity < payload.size() + 1) {
      copy_text(error, error_capacity, "DEPENDENCY_EVALUATION_BUFFER_TOO_SMALL");
      return 0;
    }
    copy_text(output, output_capacity, payload);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) {
    copy_text(error, error_capacity, ex.what()); return 0;
  } catch (...) {
    copy_text(error, error_capacity, "UNKNOWN_DEPENDENCY_EVALUATION_EXCEPTION"); return 0;
  }
}



int surge_context_graph_set_focus_region(void* opaque,const char* delta_id,
    const char* idempotency_key,uint64_t expected,const SurgeFocusRegionInput* input,
    int* duplicate,uint64_t* state_version,char* error,size_t error_capacity) {
  try {
    if(!opaque||!delta_id||!*delta_id||!idempotency_key||!*idempotency_key||!input){
      copy_text(error,error_capacity,"FOCUS_REGION_ARGUMENT_INVALID");return 0;
    }
    auto& handle=*static_cast<Handle*>(opaque);
    surge::SemanticDelta delta;delta.delta_id=delta_id;delta.idempotency_key=idempotency_key;
    delta.expected_state_version=expected;
    const surge::FocusRegion region{{input->center_x,input->center_y,input->center_z},
      {input->half_extent_x,input->half_extent_y,input->half_extent_z}};
    std::string detail;
    if(!handle.graph.append_focus_region_update(delta,region,detail)){
      copy_text(error,error_capacity,detail);return 0;
    }
    const auto result=handle.mutations.apply(delta);
    if(duplicate)*duplicate=result.duplicate?1:0;
    if(state_version)*state_version=result.state_version;
    copy_text(error,error_capacity,result.error);return result.committed?1:0;
  }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
   catch(...){copy_text(error,error_capacity,"FOCUS_REGION_UNKNOWN_EXCEPTION");return 0;}
}

int surge_context_graph_set_cluster_spatial_position(void* opaque,const char* delta_id,
    const char* idempotency_key,uint64_t expected,
    const SurgeClusterSpatialPositionInput* input,int* duplicate,
    uint64_t* state_version,char* error,size_t error_capacity) {
  try {
    if(!opaque||!delta_id||!*delta_id||!idempotency_key||!*idempotency_key||
       !input||!input->cluster_id||!*input->cluster_id){
      copy_text(error,error_capacity,"CLUSTER_POSITION_ARGUMENT_INVALID");return 0;
    }
    auto& handle=*static_cast<Handle*>(opaque);
    surge::SemanticDelta delta;delta.delta_id=delta_id;delta.idempotency_key=idempotency_key;
    delta.expected_state_version=expected;
    std::string detail;
    if(!handle.graph.append_cluster_spatial_position_update(delta,
        {input->cluster_id,{input->x,input->y,input->z}},detail)){
      copy_text(error,error_capacity,detail);return 0;
    }
    const auto result=handle.mutations.apply(delta);
    if(duplicate)*duplicate=result.duplicate?1:0;
    if(state_version)*state_version=result.state_version;
    copy_text(error,error_capacity,result.error);return result.committed?1:0;
  }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
   catch(...){copy_text(error,error_capacity,"CLUSTER_POSITION_UNKNOWN_EXCEPTION");return 0;}
}

int surge_context_graph_set_cluster_focus_depth(void* opaque,const char* delta_id,
    const char* idempotency_key,uint64_t expected,
    const SurgeClusterFocusDepthInput* input,int* duplicate,
    uint64_t* state_version,char* error,size_t error_capacity) {
  try {
    if(!opaque||!delta_id||!*delta_id||!idempotency_key||!*idempotency_key||
       !input||!input->cluster_id||!*input->cluster_id){
      copy_text(error,error_capacity,"CLUSTER_DEPTH_ARGUMENT_INVALID");return 0;
    }
    auto& handle=*static_cast<Handle*>(opaque);
    surge::SemanticDelta delta;delta.delta_id=delta_id;delta.idempotency_key=idempotency_key;
    delta.expected_state_version=expected;
    std::string detail;
    if(!handle.graph.append_cluster_focus_depth_update(delta,
        {input->cluster_id,input->depth},detail)){
      copy_text(error,error_capacity,detail);return 0;
    }
    const auto result=handle.mutations.apply(delta);
    if(duplicate)*duplicate=result.duplicate?1:0;
    if(state_version)*state_version=result.state_version;
    copy_text(error,error_capacity,result.error);return result.committed?1:0;
  }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
   catch(...){copy_text(error,error_capacity,"CLUSTER_DEPTH_UNKNOWN_EXCEPTION");return 0;}
}

int surge_context_graph_observe_focused_cognition(void* opaque,size_t max_nodes,
    size_t max_relations,char* output,size_t output_capacity,
    size_t* required_capacity,char* error,size_t error_capacity) {
  try {
    if(!opaque||max_nodes>1000000||max_relations>2000000){
      copy_text(error,error_capacity,"FOCUSED_OBSERVATION_ARGUMENT_INVALID");return 0;
    }
    const auto observation=static_cast<Handle*>(opaque)->graph.observe_focused_cognition(
        max_nodes,max_relations);
    std::ostringstream json;
    json<<"{\"state_version\":"<<observation.state_version<<",\"focus_region\":";
    if(observation.focus_region){
      const auto& r=*observation.focus_region;
      json<<"{\"center\":["<<std::setprecision(17)<<r.center.x<<','<<r.center.y<<','<<r.center.z
          <<"],\"half_extents\":["<<r.half_extents.x<<','<<r.half_extents.y<<','<<r.half_extents.z<<"]}";
    }else json<<"null";
    auto write_ids=[&](const char* key,const std::vector<std::string>& ids){
      json<<",\""<<key<<"\":[";
      for(size_t i=0;i<ids.size();++i){if(i)json<<',';json<<json_quote(ids[i]);}
      json<<']';
    };
    write_ids("logical_region_cluster_ids",observation.logical_region_cluster_ids);
    write_ids("processed_region_cluster_ids",observation.processed_region_cluster_ids);
    write_ids("deferred_region_cluster_ids",observation.deferred_region_cluster_ids);
    json<<",\"work\":{\"clusters_considered\":"<<observation.focus_work.clusters_considered
        <<",\"clusters_in_region\":"<<observation.focus_work.clusters_in_region
        <<",\"clusters_scheduled\":"<<observation.focus_work.clusters_scheduled
        <<",\"clusters_deferred\":"<<observation.focus_work.clusters_deferred
        <<",\"position_checks\":"<<observation.focus_work.position_checks
        <<",\"nodes_processed\":"<<observation.focus_work.nodes_processed
        <<",\"relations_processed\":"<<observation.focus_work.relations_processed
        <<",\"work_units\":"<<observation.focus_work.work_units
        <<",\"elapsed_microseconds\":"<<observation.focus_work.elapsed_microseconds
        <<",\"max_nodes\":"<<observation.focus_work.max_nodes
        <<",\"max_relations\":"<<observation.focus_work.max_relations
        <<",\"budget_exhausted\":"<<(observation.focus_work.budget_exhausted?"true":"false")<<"}";
    json<<",\"cluster_focus_depths\":[";
    for(size_t i=0;i<observation.cluster_focus_depths.size();++i){
      if(i)json<<',';const auto& d=observation.cluster_focus_depths[i];
      json<<"{\"cluster_id\":"<<json_quote(d.cluster_id)<<",\"depth\":"<<d.depth<<"}";
    }
    json<<"],\"nodes\":[";
    for(size_t i=0;i<observation.nodes.size();++i){
      if(i)json<<',';const auto& n=observation.nodes[i];
      json<<"{\"id\":"<<json_quote(n.id)<<",\"kind\":"<<json_quote(node_kind_name(n.kind))
          <<",\"content\":"<<json_quote(n.content)<<"}";
    }
    json<<"],\"relations\":[";
    for(size_t i=0;i<observation.relations.size();++i){
      if(i)json<<',';const auto& rel=observation.relations[i];
      json<<"{\"id\":"<<json_quote(rel.id)<<",\"kind\":"<<json_quote(relation_kind_name(rel.kind))
          <<",\"from\":"<<json_quote(rel.from)<<",\"to\":"<<json_quote(rel.to)
          <<",\"weight\":"<<std::setprecision(17)<<rel.weight<<"}";
    }
    json<<"]}";
    const auto payload=json.str();
    if(required_capacity)*required_capacity=payload.size()+1;
    if(!output||output_capacity<payload.size()+1){
      copy_text(error,error_capacity,"FOCUSED_OBSERVATION_BUFFER_TOO_SMALL");return 0;
    }
    copy_text(output,output_capacity,payload);copy_text(error,error_capacity,"");return 1;
  }catch(const std::exception& ex){copy_text(error,error_capacity,ex.what());return 0;}
   catch(...){copy_text(error,error_capacity,"FOCUSED_OBSERVATION_UNKNOWN_EXCEPTION");return 0;}
}

int surge_context_graph_observe_cognition(void* opaque, char* output,
    size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity) {
  try {
    if (!opaque) {
      copy_text(error, error_capacity, "COGNITION_OBSERVATION_HANDLE_INVALID");
      return 0;
    }
    const auto observation =
        static_cast<Handle*>(opaque)->mutations.observe_cognition();
    const auto node_kind_name = [](surge::NodeKind kind) -> const char* {
      switch (kind) {
        case surge::NodeKind::Cluster: return "CLUSTER";
        case surge::NodeKind::ChaosCluster: return "CHAOS_CLUSTER";
        case surge::NodeKind::Entity: return "ENTITY";
        case surge::NodeKind::Evidence: return "EVIDENCE";
        case surge::NodeKind::Claim: return "CLAIM";
        case surge::NodeKind::Question: return "QUESTION";
        case surge::NodeKind::QuestionAspect: return "QUESTION_ASPECT";
        case surge::NodeKind::Dependency: return "DEPENDENCY";
        case surge::NodeKind::Interest: return "INTEREST";
        case surge::NodeKind::ClusterLifecycle: return "CLUSTER_LIFECYCLE";
      }
      return "UNKNOWN";
    };
    const auto relation_kind_name = [](surge::RelationKind kind) -> const char* {
      switch (kind) {
        case surge::RelationKind::Supports: return "SUPPORTS";
        case surge::RelationKind::Contradicts: return "CONTRADICTS";
        case surge::RelationKind::AgreesWith: return "AGREES_WITH";
        case surge::RelationKind::ConflictsWith: return "CONFLICTS_WITH";
        case surge::RelationKind::DependsOn: return "DEPENDS_ON";
        case surge::RelationKind::About: return "ABOUT";
        case surge::RelationKind::Supersedes: return "SUPERSEDES";
        case surge::RelationKind::Retracts: return "RETRACTS";
        case surge::RelationKind::Duplicates: return "DUPLICATES";
        case surge::RelationKind::Contains: return "CONTAINS";
        case surge::RelationKind::DerivedFrom: return "DERIVED_FROM";
      }
      return "UNKNOWN";
    };
    std::ostringstream json;
    json << "{\"state_version\":" << observation.state_version << ",\"nodes\":[";
    for (size_t i = 0; i < observation.nodes.size(); ++i) {
      if (i) json << ',';
      const auto& node = observation.nodes[i];
      json << "{\"id\":" << json_quote(node.id)
           << ",\"kind\":" << json_quote(node_kind_name(node.kind))
           << ",\"content\":" << json_quote(node.content)
           << ",\"references\":[";
      for (size_t j = 0; j < node.references.size(); ++j) {
        if (j) json << ',';
        json << json_quote(node.references[j]);
      }
      json << "]}";
    }
    json << "],\"relations\":[";
    for (size_t i = 0; i < observation.relations.size(); ++i) {
      if (i) json << ',';
      const auto& relation = observation.relations[i];
      json << "{\"id\":" << json_quote(relation.id)
           << ",\"kind\":" << json_quote(relation_kind_name(relation.kind))
           << ",\"from\":" << json_quote(relation.from)
           << ",\"to\":" << json_quote(relation.to)
           << ",\"weight\":" << std::setprecision(17) << relation.weight << "}";
    }
    json << "],\"memberships\":[";
    for (size_t i = 0; i < observation.memberships.size(); ++i) {
      if (i) json << ',';
      const auto& membership = observation.memberships[i];
      json << "{\"member_node_id\":" << json_quote(membership.member_node_id)
           << ",\"cluster_node_id\":" << json_quote(membership.cluster_node_id)
           << ",\"upstream_weight\":" << std::setprecision(17)
           << membership.upstream_weight << "}";
    }
    json << "],\"unresolved_member_ids\":[";
    for (size_t i = 0; i < observation.unresolved_member_ids.size(); ++i) {
      if (i) json << ',';
      json << json_quote(observation.unresolved_member_ids[i]);
    }
    json << "],\"active_question_ids\":[";
    for (size_t i = 0; i < observation.active_question_ids.size(); ++i) {
      if (i) json << ',';
      json << json_quote(observation.active_question_ids[i]);
    }
    json << "],\"active_interest_ids\":[";
    for (size_t i = 0; i < observation.active_interest_ids.size(); ++i) {
      if (i) json << ',';
      json << json_quote(observation.active_interest_ids[i]);
    }
    json << "],\"current_focus\":";
    if (observation.current_focus) {
      json << "{\"cluster_id\":" << json_quote(observation.current_focus->cluster_id)
           << ",\"interest_id\":" << json_quote(observation.current_focus->interest_id)
           << ",\"chaos_cluster\":"
           << (observation.current_focus->chaos_cluster ? "true" : "false") << "}";
    } else {
      json << "null";
    }
    json << ",\"focus_history\":[";
    for (size_t i = 0; i < observation.focus_history.size(); ++i) {
      if (i) json << ',';
      const auto& focus = observation.focus_history[i];
      json << "{\"cluster_id\":" << json_quote(focus.cluster_id)
           << ",\"interest_id\":" << json_quote(focus.interest_id)
           << ",\"chaos_cluster\":" << (focus.chaos_cluster ? "true" : "false") << "}";
    }
    json << "],\"unresolved_scope_count\":" << observation.unresolved_scope_count
         << ",\"dependency_evaluation_count\":"
         << observation.dependency_evaluation_count << "}";
    const auto payload = json.str();
    if (required_capacity) *required_capacity = payload.size() + 1;
    if (!output || output_capacity < payload.size() + 1) {
      copy_text(error, error_capacity, "COGNITION_OBSERVATION_BUFFER_TOO_SMALL");
      return 0;
    }
    copy_text(output, output_capacity, payload);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) {
    copy_text(error, error_capacity, ex.what());
    return 0;
  } catch (...) {
    copy_text(error, error_capacity, "UNKNOWN_COGNITION_OBSERVATION_EXCEPTION");
    return 0;
  }
}



int surge_context_graph_recall_candidates(void* opaque, const char* input_member_id,
    const char* const* query_terms, size_t query_term_count, size_t max_candidates,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity) {
  try {
    if (!opaque || !input_member_id || (!query_terms && query_term_count) ||
        query_term_count > 4096 || max_candidates > 1024) {
      copy_text(error, error_capacity, "COGNITION_RECALL_ARGUMENT_INVALID");
      return 0;
    }
    std::vector<std::string> terms;
    terms.reserve(query_term_count);
    for (size_t i = 0; i < query_term_count; ++i) {
      if (!query_terms[i]) {
        copy_text(error, error_capacity, "COGNITION_RECALL_TERM_INVALID");
        return 0;
      }
      terms.emplace_back(query_terms[i]);
    }
    const auto& handle = *static_cast<Handle*>(opaque);
    const auto candidates = handle.mutations.recall_cognition_candidates(
        input_member_id, terms, max_candidates);
    std::ostringstream json;
    json << "{\"state_version\":" << handle.journal.state_version()
         << ",\"candidates\":[";
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (i) json << ',';
      const auto& candidate = candidates[i];
      json << "{\"cluster_id\":" << json_quote(candidate.cluster_id)
           << ",\"recall_score\":" << std::setprecision(17)
           << candidate.recall_score
           << ",\"memory_state_version\":" << candidate.memory_state_version
           << ",\"supporting_member_ids\":[";
      for (size_t j = 0; j < candidate.supporting_member_ids.size(); ++j) {
        if (j) json << ',';
        json << json_quote(candidate.supporting_member_ids[j]);
      }
      json << "],\"matched_terms\":[";
      for (size_t j = 0; j < candidate.matched_terms.size(); ++j) {
        if (j) json << ',';
        json << json_quote(candidate.matched_terms[j]);
      }
      json << "]}";
    }
    json << "]}";
    const auto payload = json.str();
    if (required_capacity) *required_capacity = payload.size() + 1;
    if (!output || output_capacity < payload.size() + 1) {
      copy_text(error, error_capacity, "COGNITION_RECALL_BUFFER_TOO_SMALL");
      return 0;
    }
    copy_text(output, output_capacity, payload);
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) {
    copy_text(error, error_capacity, ex.what()); return 0;
  } catch (...) {
    copy_text(error, error_capacity, "UNKNOWN_COGNITION_RECALL_EXCEPTION"); return 0;
  }
}

int surge_context_graph_record_cognition_feedback(void* opaque,
    const char* feedback_id, const char* candidate_cluster_id,
    const char* input_member_id, const char* outcome, const char* producer_id,
    const char* producer_version, const char* policy_version,
    const char* recorded_at, const char* notes, uint64_t expected_state_version,
    const char* feedback_node_id, const char* relation_id,
    uint64_t* state_version, char* error, size_t error_capacity) {
  try {
    if (!opaque || !feedback_id || !candidate_cluster_id || !input_member_id ||
        !outcome || !producer_id || !producer_version || !policy_version ||
        !recorded_at || !feedback_node_id || !relation_id) {
      copy_text(error, error_capacity, "COGNITION_FEEDBACK_ARGUMENT_INVALID");
      return 0;
    }
    auto& handle = *static_cast<Handle*>(opaque);
    surge::SemanticDelta delta;
    delta.delta_id = std::string("cognition-feedback/") + feedback_id;
    delta.idempotency_key = delta.delta_id;
    delta.expected_state_version = expected_state_version;
    surge::CognitionFeedback feedback;
    feedback.feedback_id = feedback_id;
    feedback.candidate_cluster_id = candidate_cluster_id;
    feedback.input_member_id = input_member_id;
    feedback.outcome = outcome;
    feedback.producer_id = producer_id;
    feedback.producer_version = producer_version;
    feedback.policy_version = policy_version;
    feedback.recorded_at = recorded_at;
    feedback.notes = notes ? notes : "";
    std::string detail;
    const auto result = handle.mutations.append_cognition_feedback(
        std::move(delta), feedback, feedback_node_id, relation_id, detail);
    if (!result) {
      copy_text(error, error_capacity, detail.empty() ?
          "COGNITION_FEEDBACK_COMMIT_FAILED" : detail);
      return 0;
    }
    if (state_version) *state_version = handle.journal.state_version();
    copy_text(error, error_capacity, "");
    return 1;
  } catch (const std::exception& ex) {
    copy_text(error, error_capacity, ex.what()); return 0;
  } catch (...) {
    copy_text(error, error_capacity, "UNKNOWN_COGNITION_FEEDBACK_EXCEPTION"); return 0;
  }
}

int surge_temporal_boundary_add(void*o,const char*t,const char*i,const char*e,uint64_t v,const char*c,const char*r,const char*f,const char*u,const char*b,const char*p,char*out,size_t cap,size_t*req,char*err,size_t ec){try{if(!o||!t||!i||!e||!c||!r||!f||!u||!b||!p){copy_text(err,ec,"TEMPORAL_LEDGER_ARGUMENT_INVALID");return 0;}surge::TemporalBoundaryContribution x{c,r,f,u,b,p};auto&h=*static_cast<Handle*>(o);auto z=h.temporal_ledger.add(t,i,e,v,x);
if(z.committed){bool already_synced=false;for(const auto&q:h.temporal_questions.for_claim(z.pending.claim_id))if(q.last_event_id==std::string(e)+"/question")already_synced=true;
if(!already_synced){auto sync=h.temporal_questions.synchronize(std::string(t)+"/question",std::string(i)+"/question",std::string(e)+"/question",z.state_version,z.pending);
if(!sync.committed&&sync.status!="NO_TEMPORAL_CONFLICT_QUESTION"){copy_text(err,ec,sync.error.empty()?sync.status:sync.error);return 0;}}
for(const auto&candidate:z.pending.candidates){surge::TemporalQuestion question;if(h.temporal_questions.get(z.pending.claim_id,candidate.role,question)&&!ensure_temporal_question_route(h,question)){copy_text(err,ec,"TEMPORAL_QUESTION_FOCUS_ROUTE_FAILED");return 0;}}
z.state_version=h.journal.state_version();}
auto q=temporal_result_json(z);if(req)*req=q.size()+1;if(!out||cap<q.size()+1){copy_text(err,ec,"TEMPORAL_LEDGER_BUFFER_TOO_SMALL");return 0;}copy_text(out,cap,q);copy_text(err,ec,"");return z.committed?1:0;}catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"TEMPORAL_LEDGER_UNKNOWN_EXCEPTION");return 0;}}
int surge_temporal_boundary_get(void*o,const char*c,char*out,size_t cap,size_t*req,char*err,size_t ec){try{if(!o||!c||!*c){copy_text(err,ec,"CLAIM_ID_REQUIRED");return 0;}surge::TemporalBoundaryPending p;bool found=static_cast<Handle*>(o)->temporal_ledger.get(c,p);auto q=found?std::string(R"({"found":true,"pending":)")+temporal_pending_json(p)+"}":std::string(R"({"found":false,"pending":null})");if(req)*req=q.size()+1;if(!out||cap<q.size()+1){copy_text(err,ec,"TEMPORAL_LEDGER_BUFFER_TOO_SMALL");return 0;}copy_text(out,cap,q);copy_text(err,ec,"");return 1;}catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"TEMPORAL_LEDGER_UNKNOWN_EXCEPTION");return 0;}}
int surge_temporal_boundary_clear(void*o,const char*t,const char*i,const char*e,uint64_t v,const char*c,char*out,size_t cap,size_t*req,char*err,size_t ec){try{if(!o||!t||!i||!e||!c||!*c){copy_text(err,ec,"TEMPORAL_LEDGER_ARGUMENT_INVALID");return 0;}auto&l=static_cast<Handle*>(o)->temporal_ledger;auto z=l.clear(t,i,e,v,c);auto q=temporal_result_json(z);if(req)*req=q.size()+1;if(!out||cap<q.size()+1){copy_text(err,ec,"TEMPORAL_LEDGER_BUFFER_TOO_SMALL");return 0;}copy_text(out,cap,q);copy_text(err,ec,"");return z.committed?1:0;}catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"TEMPORAL_LEDGER_UNKNOWN_EXCEPTION");return 0;}}
int surge_temporal_boundary_resolve_candidate(void*o,const char*t,const char*i,const char*e,uint64_t v,const char*c,const char*r,const char*d,char*out,size_t cap,size_t*req,char*err,size_t ec){try{if(!o||!t||!i||!e||!c||!r||!d){copy_text(err,ec,"TEMPORAL_LEDGER_ARGUMENT_INVALID");return 0;}auto&h=*static_cast<Handle*>(o);auto z=h.temporal_ledger.resolve_candidate(t,i,e,v,c,r,d);
if(z.committed){bool already_synced=false;for(const auto&q:h.temporal_questions.for_claim(z.pending.claim_id))if(q.last_event_id==std::string(e)+"/question")already_synced=true;
if(!already_synced){auto sync=h.temporal_questions.synchronize(std::string(t)+"/question",std::string(i)+"/question",std::string(e)+"/question",z.state_version,z.pending);
if(!sync.committed&&sync.status!="NO_TEMPORAL_CONFLICT_QUESTION"){copy_text(err,ec,sync.error.empty()?sync.status:sync.error);return 0;}}
for(const auto&candidate:z.pending.candidates){surge::TemporalQuestion question;if(h.temporal_questions.get(z.pending.claim_id,candidate.role,question)&&!ensure_temporal_question_route(h,question)){copy_text(err,ec,"TEMPORAL_QUESTION_FOCUS_ROUTE_FAILED");return 0;}}
z.state_version=h.journal.state_version();}
auto q=temporal_result_json(z);if(req)*req=q.size()+1;if(!out||cap<q.size()+1){copy_text(err,ec,"TEMPORAL_LEDGER_BUFFER_TOO_SMALL");return 0;}copy_text(out,cap,q);copy_text(err,ec,"");return z.committed?1:0;}catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"TEMPORAL_LEDGER_UNKNOWN_EXCEPTION");return 0;}}
int surge_temporal_question_get(void*o,const char*c,char*out,size_t cap,size_t*req,char*err,size_t ec){
 try{if(!o||!c||!*c){copy_text(err,ec,"CLAIM_ID_REQUIRED");return 0;}
 auto questions=static_cast<Handle*>(o)->temporal_questions.for_claim(c);std::ostringstream j;
 j<<R"({"found":)"<<(questions.empty()?"false":"true")<<R"(,"questions":[)";
 for(size_t i=0;i<questions.size();++i){if(i)j<<',';j<<temporal_question_json(questions[i]);}j<<"]}";
 auto value=j.str();if(req)*req=value.size()+1;if(!out||cap<value.size()+1){copy_text(err,ec,"TEMPORAL_QUESTION_BUFFER_TOO_SMALL");return 0;}
 copy_text(out,cap,value);copy_text(err,ec,"");return 1;
 }catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"TEMPORAL_QUESTION_UNKNOWN_EXCEPTION");return 0;}
}
size_t surge_temporal_question_count(void*o){try{return o?static_cast<Handle*>(o)->temporal_questions.size():0;}catch(...){return 0;}}
int surge_context_graph_focus_get(void*o,char*out,size_t cap,size_t*req,char*err,size_t ec){
 try{if(!o){copy_text(err,ec,"CORE_HANDLE_REQUIRED");return 0;}
 const auto& focus=static_cast<Handle*>(o)->graph.focus();std::string value;
 if(focus)value=std::string(R"({"found":true,"cluster_id":)")+json_quote(focus->cluster_id)+R"(,"interest_id":)"+json_quote(focus->interest_id)+R"(,"target_kind":)"+json_quote(focus->chaos_cluster?"CHAOS_CLUSTER":"CLUSTER")+"}";
 else value=R"({"found":false,"cluster_id":null,"interest_id":null})";
 if(req) *req=value.size()+1;
 if(!out||cap<value.size()+1){copy_text(err,ec,"FOCUS_BUFFER_TOO_SMALL");return 0;}
 copy_text(out,cap,value);copy_text(err,ec,"");return 1;
 }catch(const std::exception&x){copy_text(err,ec,x.what());return 0;}catch(...){copy_text(err,ec,"FOCUS_QUERY_UNKNOWN_EXCEPTION");return 0;}
}
size_t surge_temporal_boundary_pending_count(void* opaque) {
  try { return opaque?static_cast<Handle*>(opaque)->temporal_ledger.pending_claim_count():0; }
  catch(...) { return 0; }
}


int surge_context_graph_focus_set_chaos(void* opaque,const char* delta_id,const char* idempotency_key,
    uint64_t expected,const char* chaos_cluster_id,const char* interest_id,
    int* duplicate,uint64_t* state_version,char* error,size_t error_cap) {
  try {
    if(!opaque||!delta_id||!*delta_id||!idempotency_key||!*idempotency_key||!chaos_cluster_id||!*chaos_cluster_id) {
      copy_text(error,error_cap,"CHAOS_FOCUS_ARGUMENT_INVALID");return 0;
    }
    auto& handle=*static_cast<Handle*>(opaque);
    surge::ChaosCluster cluster;
    if(!handle.chaos.get(chaos_cluster_id,cluster)) { copy_text(error,error_cap,"CHAOS_FOCUS_TARGET_NOT_FOUND");return 0; }
    surge::SemanticDelta delta;delta.delta_id=delta_id;delta.idempotency_key=idempotency_key;delta.expected_state_version=expected;
    const auto existing=handle.graph.nodes().find(chaos_cluster_id);
    if(existing==handle.graph.nodes().end()) delta.nodes.push_back({{chaos_cluster_id,surge::NodeKind::ChaosCluster,"Chaos Cluster "+cluster.cluster_id,{}}});
    else if(existing->second.kind!=surge::NodeKind::ChaosCluster) { copy_text(error,error_cap,"CHAOS_FOCUS_GRAPH_ID_CONFLICT");return 0; }
    delta.focus=surge::SetFocus{{chaos_cluster_id,interest_id?interest_id:"",true}};
    const auto result=handle.mutations.apply(delta);
    if(duplicate) *duplicate=result.duplicate?1:0;
    if(state_version) *state_version=result.state_version;
    copy_text(error,error_cap,result.error);return result.committed?1:0;
  } catch(const std::exception& ex){copy_text(error,error_cap,ex.what());return 0;}
    catch(...){copy_text(error,error_cap,"CHAOS_FOCUS_UNKNOWN_EXCEPTION");return 0;}
}

extern "C++" {
namespace {
bool chaos_get_provenance(const SurgeChaosProvenanceInput* p,surge::ChaosProvenance& out) {
 if(!p||!p->producer_id||!p->producer_version||!p->policy_version||!p->recorded_at)return false;
 out={p->producer_id,p->producer_version,p->policy_version,p->recorded_at};return true;
}
bool chaos_get_lifecycle(const char* s,surge::ChaosLifecycle& out) {
 if(!s) return false;
 const std::string v(s);
 if(v=="OPEN") out=surge::ChaosLifecycle::Open;
 else if(v=="ACCUMULATING") out=surge::ChaosLifecycle::Accumulating;
 else if(v=="EMERGING") out=surge::ChaosLifecycle::Emerging;
 else if(v=="REVIEWABLE") out=surge::ChaosLifecycle::Reviewable;
 else if(v=="PROMOTED") out=surge::ChaosLifecycle::Promoted;
 else if(v=="FORKED") out=surge::ChaosLifecycle::Forked;
 else if(v=="DORMANT") out=surge::ChaosLifecycle::Dormant;
 else if(v=="ARCHIVED") out=surge::ChaosLifecycle::Archived;
 else return false;
 return true;
}
bool chaos_get_type(const char* s,surge::ChaosAssociationType& out) {
 if(!s) return false;
 const std::string v(s);
 if(v=="CO_OCCURS") out=surge::ChaosAssociationType::CoOccurs;
 else if(v=="ANALOGY") out=surge::ChaosAssociationType::Analogy;
 else if(v=="HYPOTHESIS") out=surge::ChaosAssociationType::Hypothesis;
 else if(v=="QUESTION") out=surge::ChaosAssociationType::Question;
 else if(v=="CONTRADICTION") out=surge::ChaosAssociationType::Contradiction;
 else if(v=="TEMPORAL_NEARNESS") out=surge::ChaosAssociationType::TemporalNearness;
 else if(v=="UNCLASSIFIED") out=surge::ChaosAssociationType::Unclassified;
 else if(v=="CREATIVE_SYNTHESIS") out=surge::ChaosAssociationType::CreativeSynthesis;
 else return false;
 return true;
}
std::string chaos_cluster_json_bounded(const surge::ChaosCluster& c,
    size_t max_members, size_t max_associations, size_t max_hypotheses,
    size_t max_history, size_t max_refs) {
 auto lifecycle=[](surge::ChaosLifecycle v)->const char* {
  switch(v){case surge::ChaosLifecycle::Open:return "OPEN";case surge::ChaosLifecycle::Accumulating:return "ACCUMULATING";
   case surge::ChaosLifecycle::Emerging:return "EMERGING";case surge::ChaosLifecycle::Reviewable:return "REVIEWABLE";
   case surge::ChaosLifecycle::Promoted:return "PROMOTED";case surge::ChaosLifecycle::Forked:return "FORKED";
   case surge::ChaosLifecycle::Dormant:return "DORMANT";case surge::ChaosLifecycle::Archived:return "ARCHIVED";}return "UNKNOWN";
 };
 auto organization=[](surge::ChaosOrganization v)->const char* {
  switch(v){case surge::ChaosOrganization::Unordered:return "UNORDERED";case surge::ChaosOrganization::Provisional:return "PROVISIONAL";
   case surge::ChaosOrganization::MultiPattern:return "MULTI_PATTERN";case surge::ChaosOrganization::PartiallyStructured:return "PARTIALLY_STRUCTURED";}return "UNKNOWN";
 };
 auto provenance=[](const surge::ChaosProvenance& p) {
  return std::string("{\"producer_id\":")+json_quote(p.producer_id)+",\"producer_version\":"+json_quote(p.producer_version)+
   ",\"policy_version\":"+json_quote(p.policy_version)+",\"recorded_at\":"+json_quote(p.recorded_at)+"}";
 };
 auto observation_json=[](const surge::ChaosScoreObservation& v){
  return std::string("{\"value\":")+([&](){std::ostringstream n;n<<std::setprecision(17)<<v.value;return n.str();})()+
   ",\"scorer_id\":"+json_quote(v.scorer_id)+",\"scorer_version\":"+json_quote(v.scorer_version)+
   ",\"policy_version\":"+json_quote(v.policy_version)+",\"observed_at\":"+json_quote(v.observed_at)+"}";
 };
 auto observation_history_json=[&](const std::vector<surge::ChaosScoreObservation>& history,size_t& omitted){
  const size_t count=std::min(history.size(),max_history),start=history.size()-count;omitted+=start;
  std::ostringstream out;out<<"{\"history_offset\":"<<start<<",\"history\":[";
  for(size_t i=start;i<history.size();++i){if(i>start)out<<',';out<<observation_json(history[i]);}
  out<<"]}";return out.str();
 };
 auto status=[](surge::ChaosAssociationStatus v)->const char* {
  switch(v){case surge::ChaosAssociationStatus::Provisional:return "PROVISIONAL";case surge::ChaosAssociationStatus::Reviewed:return "REVIEWED";
   case surge::ChaosAssociationStatus::Rejected:return "REJECTED";case surge::ChaosAssociationStatus::Expired:return "EXPIRED";}return "UNKNOWN";
 };
 auto type=[](surge::ChaosAssociationType v)->const char* {
  switch(v){case surge::ChaosAssociationType::CoOccurs:return "CO_OCCURS";case surge::ChaosAssociationType::Analogy:return "ANALOGY";
   case surge::ChaosAssociationType::Hypothesis:return "HYPOTHESIS";case surge::ChaosAssociationType::Question:return "QUESTION";
   case surge::ChaosAssociationType::Contradiction:return "CONTRADICTION";case surge::ChaosAssociationType::TemporalNearness:return "TEMPORAL_NEARNESS";
   case surge::ChaosAssociationType::CreativeSynthesis:return "CREATIVE_SYNTHESIS";case surge::ChaosAssociationType::Unclassified:return "UNCLASSIFIED";}return "UNKNOWN";
 };
 auto refs_json=[&](const std::vector<std::string>& refs,size_t& omitted) {
  std::ostringstream j;j<<'[';const size_t n=std::min(refs.size(),max_refs);
  for(size_t i=0;i<n;++i){if(i)j<<',';j<<json_quote(refs[i]);}
  j<<']';omitted+=refs.size()-n;return j.str();
 };
 size_t omitted_history=0,omitted_hypothesis_refs=0,omitted_membership_history=0,omitted_entity_observations=0,omitted_hypothesis_history=0,omitted_lifecycle_history=0;
 const size_t member_count=std::min(c.members.size(),max_members);
 const size_t association_count=std::min(c.associations.size(),max_associations);
 const size_t hypothesis_count=std::min(c.pattern_hypotheses.size(),max_hypotheses);
 std::ostringstream out;
 out<<"{\"cluster_id\":"<<json_quote(c.cluster_id)<<",\"state_version\":"<<c.state_version
    <<",\"lifecycle\":"<<json_quote(lifecycle(c.lifecycle))
    <<",\"organization\":"<<json_quote(organization(c.organization))
    <<",\"creation_provenance\":"<<provenance(c.creation_provenance);
 const size_t lifecycle_count=std::min(c.lifecycle_history.size(),max_history);
 const size_t lifecycle_start=c.lifecycle_history.size()-lifecycle_count;
 omitted_lifecycle_history+=lifecycle_start;
 out<<",\"lifecycle_history_offset\":"<<lifecycle_start<<",\"lifecycle_history\":[";
 for(size_t li=lifecycle_start;li<c.lifecycle_history.size();++li){if(li>lifecycle_start)out<<',';const auto&revision=c.lifecycle_history[li];
  out<<"{\"lifecycle\":"<<json_quote(revision.lifecycle)<<",\"reason\":"<<json_quote(revision.reason)
     <<",\"provenance\":"<<provenance(revision.provenance)<<'}';
 }
 out<<"],\"members\":[";
 size_t index=0;
 for(const auto& item:c.members){if(index>=member_count)break;if(index++)out<<',';const auto&m=item.second;
  const size_t member_history_count=std::min(m.membership_history.size(),max_history);
  const size_t member_history_start=m.membership_history.size()-member_history_count;
  omitted_membership_history+=member_history_start;
  out<<"{\"member_ref\":"<<json_quote(m.member_ref)<<",\"event_ref\":"<<json_quote(m.event_ref)
     <<",\"admission_reason\":"<<json_quote(m.admission_reason)<<",\"admitted_at\":"<<json_quote(m.admitted_at)
     <<",\"source_digest\":"<<json_quote(m.source_digest)<<",\"membership_status\":"<<json_quote(m.membership_status)
     <<",\"membership_active\":"<<(m.membership_active?"true":"false")
     <<",\"membership_history_offset\":"<<member_history_start<<",\"membership_history\":[";
  for(size_t h=member_history_start;h<m.membership_history.size();++h){if(h>member_history_start)out<<',';const auto&revision=m.membership_history[h];
   out<<"{\"status\":"<<json_quote(revision.status)<<",\"active\":"<<(revision.active?"true":"false")
      <<",\"provenance\":"<<provenance(revision.provenance)<<'}';
  }
  out<<"],";
  if(!m.salience_observations.empty())out<<"\"salience\":"<<observation_json(m.salience_observations.back())<<',';
  out<<"\"salience_history\":"<<observation_history_json(m.salience_observations,omitted_entity_observations)
     <<",\"provenance\":"<<provenance(m.provenance)<<'}';
 }
 out<<"],\"associations\":[";
 index=0;
 for(const auto& item:c.associations){if(index>=association_count)break;if(index++)out<<',';const auto&a=item.second;
  const size_t history_count=std::min(a.history.size(),max_history);
  const size_t history_start=a.history.size()-history_count;omitted_history+=history_start;
  out<<"{\"association_id\":"<<json_quote(a.association_id)<<",\"from_ref\":"<<json_quote(a.from_ref)
     <<",\"to_ref\":"<<json_quote(a.to_ref)<<",\"type\":"<<json_quote(type(a.type))
     <<",\"status\":"<<json_quote(status(a.status))<<",\"valid_from\":"<<json_quote(a.valid_from)
     <<",\"valid_until\":"<<json_quote(a.valid_until)<<",\"provenance\":"<<provenance(a.provenance)
     <<",\"history_offset\":"<<history_start<<",\"history\":[";
  for(size_t h=history_start;h<a.history.size();++h){if(h>history_start)out<<',';const auto&rev=a.history[h];
   out<<"{\"status\":"<<json_quote(status(rev.status))<<",\"provenance\":"<<provenance(rev.provenance)<<'}';}
  out<<"],";
  if(!a.confidence_observations.empty())out<<"\"confidence\":"<<observation_json(a.confidence_observations.back())<<',';
  out<<"\"confidence_history\":"<<observation_history_json(a.confidence_observations,omitted_entity_observations)<<'}';
 }
 out<<"],\"pattern_hypotheses\":[";
 index=0;
 for(const auto& item:c.pattern_hypotheses){if(index>=hypothesis_count)break;if(index++)out<<',';const auto&h=item.second;
  out<<"{\"hypothesis_id\":"<<json_quote(h.hypothesis_id)<<",\"statement\":"<<json_quote(h.statement)
     <<",\"epistemic_state\":"<<json_quote(h.epistemic_state)<<",\"status\":"<<json_quote(h.status)
     <<",\"member_refs\":"<<refs_json(h.member_refs,omitted_hypothesis_refs)
     <<",\"excluded_member_refs\":"<<refs_json(h.excluded_member_refs,omitted_hypothesis_refs)
     <<",\"counterexample_refs\":"<<refs_json(h.counterexample_refs,omitted_hypothesis_refs)
     <<",\"alternative_hypothesis_refs\":"<<refs_json(h.alternative_hypothesis_refs,omitted_hypothesis_refs)
     <<",\"confidence_history\":"<<observation_history_json(h.confidence_observations,omitted_entity_observations);
  if(!h.confidence_observations.empty())out<<",\"confidence\":"<<observation_json(h.confidence_observations.back());
  out<<",\"exclusion_reasons\":{";
  bool first_reason=true;for(const auto&reason:h.exclusion_reasons){if(!first_reason)out<<',';first_reason=false;out<<json_quote(reason.first)<<':'<<json_quote(reason.second);}
  const size_t revision_count=std::min(h.history.size(),max_history),revision_start=h.history.size()-revision_count;
  omitted_hypothesis_history+=revision_start;
  out<<"},\"history_offset\":"<<revision_start<<",\"history\":[";
  for(size_t ri=revision_start;ri<h.history.size();++ri){if(ri>revision_start)out<<',';const auto&revision=h.history[ri];
   out<<"{\"status\":"<<json_quote(revision.status)<<",\"epistemic_state\":"<<json_quote(revision.epistemic_state)
      <<",\"exclusion_reasons\":{";
   bool first_revision_reason=true;for(const auto&reason:revision.exclusion_reasons){if(!first_revision_reason)out<<',';first_revision_reason=false;out<<json_quote(reason.first)<<':'<<json_quote(reason.second);}
   out<<"},\"provenance\":"<<provenance(revision.provenance)<<'}';
  }
  out<<"],\"provenance\":"<<provenance(h.provenance)<<'}';
 }
 size_t omitted_observations=0;
 out<<"],\"observations\":{";
 bool first_dimension=true;
 for(const auto& dimension:c.observations){
  if(dimension.second.empty())continue;
  if(!first_dimension) out<<',';
  first_dimension=false;
  out<<json_quote(dimension.first)<<':'<<observation_json(dimension.second.back());
 }
 out<<"},\"observation_history\":{";
 first_dimension=true;
 for(const auto& dimension:c.observations){
  if(!first_dimension) out<<',';
  first_dimension=false;
  const auto& history=dimension.second;
  const size_t count=std::min(history.size(),max_history);
  const size_t start=history.size()-count;
  omitted_observations+=start;
  out<<json_quote(dimension.first)<<":{\"history_offset\":"<<start<<",\"history\":[";
  for(size_t i=start;i<history.size();++i){if(i>start)out<<',';out<<observation_json(history[i]);}
  out<<"]}";
 }
 out<<'}';
 out<<",\"parent_refs\":[";
 for(size_t i=0;i<c.parent_refs.size();++i){if(i)out<<',';out<<json_quote(c.parent_refs[i]);}
 out<<"],\"child_refs\":[";
 for(size_t i=0;i<c.child_refs.size();++i){if(i)out<<',';out<<json_quote(c.child_refs[i]);}
 out<<"],\"promoted_member_targets\":{";
 bool first=true;for(const auto& item:c.promoted_member_targets){if(!first)out<<',';first=false;out<<json_quote(item.first)<<':'<<json_quote(item.second);}
 const size_t omitted_members=c.members.size()-member_count, omitted_associations=c.associations.size()-association_count;
 const size_t omitted_hypotheses=c.pattern_hypotheses.size()-hypothesis_count;
 out<<"},\"complete\":"<<((omitted_members||omitted_associations||omitted_hypotheses||omitted_history||omitted_hypothesis_refs||omitted_observations||omitted_membership_history||omitted_entity_observations||omitted_hypothesis_history||omitted_lifecycle_history)?"false":"true")
    <<",\"omitted\":{\"members\":"<<omitted_members<<",\"associations\":"<<omitted_associations
    <<",\"association_history_revisions\":"<<omitted_history
    <<",\"pattern_hypotheses\":"<<omitted_hypotheses
    <<",\"hypothesis_references\":"<<omitted_hypothesis_refs
    <<",\"score_observation_revisions\":"<<omitted_observations
    <<",\"membership_history_revisions\":"<<omitted_membership_history
    <<",\"entity_score_observation_revisions\":"<<omitted_entity_observations
    <<",\"pattern_hypothesis_history_revisions\":"<<omitted_hypothesis_history
    <<",\"lifecycle_history_revisions\":"<<omitted_lifecycle_history<<"}}";
 return out.str();
}
}
}
int surge_chaos_cluster_create(void* opaque,const char* op,const char* key,uint64_t expected,const char* id,const SurgeChaosProvenanceInput* input,uint64_t* version,char* error,size_t cap) {
 try {surge::ChaosProvenance p;if(!opaque||!op||!key||!id||!chaos_get_provenance(input,p)){copy_text(error,cap,"CHAOS_CLUSTER_ARGUMENT_INVALID");return 0;}
  auto r=static_cast<Handle*>(opaque)->chaos.create(op,key,expected,id,p);if(version) *version=r.state_version;
  copy_text(error,cap,r.error);
  return r.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}catch(...){copy_text(error,cap,"CHAOS_CLUSTER_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_admit_member(void* opaque,const char* op,const char* key,uint64_t expected,const char* id,const SurgeChaosMemberInput* input,const SurgeChaosProvenanceInput* provenance,uint64_t* version,char* error,size_t cap) {
 try {surge::ChaosProvenance p;if(!opaque||!op||!key||!id||!input||!input->member_ref||!input->event_ref||!input->admission_reason||!input->admitted_at||!chaos_get_provenance(provenance,p)){copy_text(error,cap,"CHAOS_MEMBER_ARGUMENT_INVALID");return 0;}
  surge::ChaosMember m;m.member_ref=input->member_ref;m.event_ref=input->event_ref;m.admission_reason=input->admission_reason;m.admitted_at=input->admitted_at;m.source_digest=input->source_digest?input->source_digest:"";m.provenance=p;
  auto r=static_cast<Handle*>(opaque)->chaos.admit_member(op,key,expected,id,m);if(version) *version=r.state_version;
  copy_text(error,cap,r.error);
  return r.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}catch(...){copy_text(error,cap,"CHAOS_MEMBER_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_propose_association(void* opaque,const char* op,const char* key,uint64_t expected,const char* id,const SurgeChaosAssociationInput* input,const SurgeChaosProvenanceInput* provenance,uint64_t* version,char* error,size_t cap) {
 try {surge::ChaosProvenance p;surge::ChaosAssociationType type;if(!opaque||!op||!key||!id||!input||!input->association_id||!input->from_member_ref||!input->to_member_ref||!chaos_get_type(input->association_type,type)||!chaos_get_provenance(provenance,p)){copy_text(error,cap,"CHAOS_ASSOCIATION_ARGUMENT_INVALID");return 0;}
  surge::ChaosAssociation a;a.association_id=input->association_id;a.from_ref=input->from_member_ref;a.to_ref=input->to_member_ref;a.type=type;a.provenance=p;
  auto r=static_cast<Handle*>(opaque)->chaos.propose_association(op,key,expected,id,a);if(version) *version=r.state_version;
  copy_text(error,cap,r.error);
  return r.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}catch(...){copy_text(error,cap,"CHAOS_ASSOCIATION_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_propose_association_temporal(void* opaque,const char* op,const char* key,uint64_t expected,
 const char* id,const SurgeChaosAssociationTemporalInput* input,const SurgeChaosProvenanceInput* provenance,
 uint64_t* version,char* error,size_t cap) {
 try {
  surge::ChaosProvenance p;surge::ChaosAssociationType type;
  if(!opaque||!op||!key||!id||!input||!input->association_id||!input->from_member_ref||
     !input->to_member_ref||!chaos_get_type(input->association_type,type)||!chaos_get_provenance(provenance,p)||
     (input->valid_from_utc&&std::strlen(input->valid_from_utc)>20)||
     (input->valid_until_utc&&std::strlen(input->valid_until_utc)>20)){
   copy_text(error,cap,"CHAOS_ASSOCIATION_TEMPORAL_ARGUMENT_INVALID");return 0;
  }
  surge::ChaosAssociation a;a.association_id=input->association_id;a.from_ref=input->from_member_ref;
  a.to_ref=input->to_member_ref;a.type=type;a.provenance=p;
  a.valid_from=input->valid_from_utc?input->valid_from_utc:"";
  a.valid_until=input->valid_until_utc?input->valid_until_utc:"";
  auto result=static_cast<Handle*>(opaque)->chaos.propose_association(op,key,expected,id,a);
  if(version)*version=result.state_version;
  copy_text(error,cap,result.error);
  return result.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_ASSOCIATION_TEMPORAL_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_merge(void* opaque,const char* op,const char* key,uint64_t expected,
 const char* target_id,const char* const* source_ids,size_t source_count,
 const SurgeChaosProvenanceInput* input,uint64_t* version,char* error,size_t cap) {
 try {
  surge::ChaosProvenance p;
  if(!opaque||!op||!key||!target_id||!source_ids||source_count==0||source_count>1000||
     !chaos_get_provenance(input,p)){
   copy_text(error,cap,"CHAOS_MERGE_ARGUMENT_INVALID");return 0;
  }
  std::vector<std::string> sources;sources.reserve(source_count);
  for(size_t i=0;i<source_count;++i){
   if(!source_ids[i]||!*source_ids[i]||std::strlen(source_ids[i])>256){
    copy_text(error,cap,"CHAOS_MERGE_SOURCE_INVALID");return 0;
   }
   sources.emplace_back(source_ids[i]);
  }
  auto result=static_cast<Handle*>(opaque)->chaos.merge_clusters(op,key,expected,target_id,sources,p);
  if(version)*version=result.state_version;
  copy_text(error,cap,result.error);
  return result.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_MERGE_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_expire_due_associations(void* opaque,const char* op,const char* key,uint64_t expected,
 const char* id,const char* as_of,const SurgeChaosProvenanceInput* input,uint64_t* version,char* error,size_t cap) {
 try {
  surge::ChaosProvenance p;
  if(!opaque||!op||!key||!id||!as_of||std::strlen(as_of)>20||!chaos_get_provenance(input,p)){
   copy_text(error,cap,"CHAOS_ASSOCIATION_EXPIRY_ARGUMENT_INVALID");return 0;
  }
  auto result=static_cast<Handle*>(opaque)->chaos.expire_due_associations(op,key,expected,id,as_of,p);
  if(version)*version=result.state_version;
  copy_text(error,cap,result.error);
  return result.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_ASSOCIATION_EXPIRY_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_transition(void* opaque,const char* op,const char* key,uint64_t expected,const char* id,const char* target,const char* reason,const SurgeChaosProvenanceInput* input,uint64_t* version,char* error,size_t cap) {
 try {surge::ChaosProvenance p;surge::ChaosLifecycle lifecycle;if(!opaque||!op||!key||!id||!reason||!chaos_get_lifecycle(target,lifecycle)||!chaos_get_provenance(input,p)){copy_text(error,cap,"CHAOS_TRANSITION_ARGUMENT_INVALID");return 0;}
  auto r=static_cast<Handle*>(opaque)->chaos.transition(op,key,expected,id,lifecycle,reason,p);if(version) *version=r.state_version;
  copy_text(error,cap,r.error);
  return r.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}catch(...){copy_text(error,cap,"CHAOS_TRANSITION_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_promote_subset(void* opaque,const char* op,const char* key,uint64_t expected,const char* id,const char* const* refs,const char* const* targets,size_t count,const SurgeChaosProvenanceInput* input,uint64_t* version,char* error,size_t cap) {
 try {surge::ChaosProvenance p;if(!opaque||!op||!key||!id||!refs||!targets||count==0||count>100000||!chaos_get_provenance(input,p)){copy_text(error,cap,"CHAOS_PROMOTION_ARGUMENT_INVALID");return 0;}
  std::map<std::string,std::string> selected;for(size_t i=0;i<count;++i)if(!refs[i]||!targets[i]||!selected.emplace(refs[i],targets[i]).second){copy_text(error,cap,"CHAOS_PROMOTION_MEMBER_INVALID");return 0;}
  auto r=static_cast<Handle*>(opaque)->chaos.promote_subset(op,key,expected,id,selected,p);if(version) *version=r.state_version;
  copy_text(error,cap,r.error);
  return r.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}catch(...){copy_text(error,cap,"CHAOS_PROMOTION_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_review_association(void* opaque,const char* op,const char* key,uint64_t expected,
    const char* id,const char* association_id,const char* target_status,const SurgeChaosProvenanceInput* input,
    uint64_t* version,char* error,size_t cap) {
 try {
  surge::ChaosProvenance p;surge::ChaosAssociationStatus status;
  if(!opaque||!op||!key||!id||!association_id||!target_status||!chaos_get_provenance(input,p)){
   copy_text(error,cap,"CHAOS_ASSOCIATION_REVIEW_ARGUMENT_INVALID");return 0;
  }
  const std::string value(target_status);
  if(value=="PROVISIONAL")status=surge::ChaosAssociationStatus::Provisional;
  else if(value=="REVIEWED")status=surge::ChaosAssociationStatus::Reviewed;
  else if(value=="REJECTED")status=surge::ChaosAssociationStatus::Rejected;
  else if(value=="EXPIRED")status=surge::ChaosAssociationStatus::Expired;
  else {copy_text(error,cap,"CHAOS_ASSOCIATION_STATUS_INVALID");return 0;}
  auto r=static_cast<Handle*>(opaque)->chaos.review_association(op,key,expected,id,association_id,status,p);
  if(version) *version=r.state_version;
  copy_text(error,cap,r.error);
  return r.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_ASSOCIATION_REVIEW_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_add_pattern_hypothesis(void* opaque,const char* op,const char* key,uint64_t expected,
    const char* id,const SurgeChaosPatternHypothesisInput* input,const SurgeChaosProvenanceInput* provenance,
    uint64_t* version,char* error,size_t cap) {
 try {
  surge::ChaosProvenance p;
  if(!opaque||!op||!key||!id||!input||!input->hypothesis_id||!input->statement||
     !input->epistemic_state||!input->status||!chaos_get_provenance(provenance,p)||
     input->member_count>10000||input->excluded_member_count>10000||
     input->counterexample_count>10000||input->alternative_hypothesis_count>10000||
     (input->member_count&&!input->member_refs)||(input->excluded_member_count&&!input->excluded_member_refs)||
     (input->counterexample_count&&!input->counterexample_refs)||
     (input->alternative_hypothesis_count&&!input->alternative_hypothesis_refs)) {
   copy_text(error,cap,"CHAOS_PATTERN_HYPOTHESIS_ARGUMENT_INVALID");return 0;
  }
  surge::ChaosPatternHypothesis h;h.hypothesis_id=input->hypothesis_id;h.statement=input->statement;
  h.epistemic_state=input->epistemic_state;h.status=input->status;h.provenance=p;
  auto copy_refs=[](const char* const* src,size_t count,std::vector<std::string>& dst) {
   for(size_t i=0;i<count;++i){if(!src[i]||!*src[i])return false;dst.emplace_back(src[i]);}
   return true;
  };
  if(!copy_refs(input->member_refs,input->member_count,h.member_refs)||
     !copy_refs(input->excluded_member_refs,input->excluded_member_count,h.excluded_member_refs)||
     !copy_refs(input->counterexample_refs,input->counterexample_count,h.counterexample_refs)||
     !copy_refs(input->alternative_hypothesis_refs,input->alternative_hypothesis_count,h.alternative_hypothesis_refs)) {
   copy_text(error,cap,"CHAOS_PATTERN_HYPOTHESIS_REFERENCE_INVALID");return 0;
  }
  auto r=static_cast<Handle*>(opaque)->chaos.add_pattern_hypothesis(op,key,expected,id,h);
  if(version) *version=r.state_version;
  copy_text(error,cap,r.error);
  return r.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_PATTERN_HYPOTHESIS_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_revise_membership(void* opaque,const char* op,const char* key,uint64_t expected,
    const char* id,const char* member_ref,const char* status,int active,
    const SurgeChaosProvenanceInput* input,uint64_t* version,char* error,size_t cap) {
 try {
  surge::ChaosProvenance provenance;
  if(!opaque||!op||!key||!id||!member_ref||!status||(active!=0&&active!=1)||
     std::strlen(op)>256||std::strlen(key)>256||std::strlen(id)>256||std::strlen(member_ref)>512||
     !chaos_get_provenance(input,provenance)) {
   copy_text(error,cap,"CHAOS_MEMBERSHIP_REVISION_ARGUMENT_INVALID");return 0;
  }
  auto result=static_cast<Handle*>(opaque)->chaos.revise_membership(
      op,key,expected,id,member_ref,status,active!=0,provenance);
  if(version)*version=result.state_version;
  copy_text(error,cap,result.error);
  return result.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_MEMBERSHIP_REVISION_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_review_pattern_hypothesis(void* opaque,const char* op,const char* key,uint64_t expected,
 const char* id,const char* hypothesis_id,const SurgeChaosHypothesisReviewInput* input,
 const SurgeChaosProvenanceInput* provenance,uint64_t* version,char* error,size_t cap) {
 try {
  surge::ChaosProvenance p;
  if(!opaque||!op||!key||!id||!hypothesis_id||!input||!input->status||!input->epistemic_state||
     input->exclusion_count>10000||
     (input->exclusion_count&&(!input->excluded_member_refs||!input->exclusion_reasons))||
     !chaos_get_provenance(provenance,p)){
   copy_text(error,cap,"CHAOS_PATTERN_HYPOTHESIS_REVIEW_ARGUMENT_INVALID");return 0;
  }
  std::map<std::string,std::string> reasons;
  for(size_t i=0;i<input->exclusion_count;++i){
   if(!input->excluded_member_refs[i]||!input->exclusion_reasons[i]||
      !*input->excluded_member_refs[i]||!*input->exclusion_reasons[i]||
      !reasons.emplace(input->excluded_member_refs[i],input->exclusion_reasons[i]).second){
    copy_text(error,cap,"CHAOS_PATTERN_HYPOTHESIS_EXCLUSION_REASON_INVALID");return 0;
   }
  }
  auto result=static_cast<Handle*>(opaque)->chaos.review_pattern_hypothesis(
      op,key,expected,id,hypothesis_id,input->status,input->epistemic_state,reasons,p);
  if(version)*version=result.state_version;
  copy_text(error,cap,result.error);
  return result.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_PATTERN_HYPOTHESIS_REVIEW_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_record_entity_observation(void* opaque,const char* op,const char* key,uint64_t expected,
    const char* id,const char* entity_kind,const char* entity_ref,
    const SurgeChaosScoreObservationInput* input,uint64_t* version,char* error,size_t cap) {
 try {
  if(!opaque||!op||!key||!id||!entity_kind||!entity_ref||!input||
     !input->scorer_id||!input->scorer_version||!input->policy_version||!input->observed_at||
     std::strlen(op)>256||std::strlen(key)>256||std::strlen(id)>256||
     std::strlen(entity_kind)>32||std::strlen(entity_ref)>512||
     std::strlen(input->scorer_id)>256||std::strlen(input->scorer_version)>128||
     std::strlen(input->policy_version)>128||std::strlen(input->observed_at)>64||
     !std::isfinite(input->value)||input->value<0.0||input->value>1.0) {
   copy_text(error,cap,"CHAOS_SCORE_OBSERVATION_ARGUMENT_INVALID");return 0;
  }
  surge::ChaosScoreObservation observation{input->value,input->scorer_id,input->scorer_version,
      input->policy_version,input->observed_at};
  auto result=static_cast<Handle*>(opaque)->chaos.record_entity_observation(
      op,key,expected,id,entity_kind,entity_ref,observation);
  if(version)*version=result.state_version;
  copy_text(error,cap,result.error);
  return result.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_ENTITY_SCORE_OBSERVATION_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_record_observation(void* opaque,const char* op,const char* key,uint64_t expected,
    const char* id,const char* dimension,const SurgeChaosScoreObservationInput* input,
    uint64_t* version,char* error,size_t cap) {
 try {
  if(!opaque||!op||!key||!id||!dimension||!input||!input->scorer_id||
     !input->scorer_version||!input->policy_version||!input->observed_at||
     std::strlen(op)>256||std::strlen(key)>256||std::strlen(id)>256||
     std::strlen(dimension)>64||std::strlen(input->scorer_id)>256||
     std::strlen(input->scorer_version)>128||std::strlen(input->policy_version)>128||
     std::strlen(input->observed_at)>64||!std::isfinite(input->value)||
     input->value<0.0||input->value>1.0) {
   copy_text(error,cap,"CHAOS_SCORE_OBSERVATION_ARGUMENT_INVALID");return 0;
  }
  surge::ChaosScoreObservation observation;
  observation.value=input->value;observation.scorer_id=input->scorer_id;
  observation.scorer_version=input->scorer_version;observation.policy_version=input->policy_version;
  observation.observed_at=input->observed_at;
  auto r=static_cast<Handle*>(opaque)->chaos.record_observation(op,key,expected,id,dimension,observation);
  if(version)*version=r.state_version;
  copy_text(error,cap,r.error);
  return r.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"CHAOS_SCORE_OBSERVATION_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_get_bounded(void* opaque,const char* id,size_t max_members,size_t max_associations,
    size_t max_hypotheses,size_t max_history,size_t max_refs,char* output,size_t output_cap,
    size_t* required,char* error,size_t error_cap) {
 try {
  if(!opaque||!id||!*id||max_members>10000||max_associations>10000||max_hypotheses>1000||
     max_history>1000||max_refs>1000){copy_text(error,error_cap,"CHAOS_QUERY_BUDGET_INVALID");return 0;}
  surge::ChaosCluster c;if(!static_cast<Handle*>(opaque)->chaos.get(id,c)){copy_text(error,error_cap,"CHAOS_CLUSTER_NOT_FOUND");return 0;}
  auto value=chaos_cluster_json_bounded(c,max_members,max_associations,max_hypotheses,max_history,max_refs);
  if(value.size()+1>16*1024*1024){copy_text(error,error_cap,"CHAOS_CLUSTER_RESPONSE_TOO_LARGE");return 0;}
  if(required)*required=value.size()+1;
  if(!output||output_cap<value.size()+1){copy_text(error,error_cap,"CHAOS_CLUSTER_BUFFER_TOO_SMALL");return 0;}
  copy_text(output,output_cap,value);copy_text(error,error_cap,"");return 1;
 } catch(const std::exception& x){copy_text(error,error_cap,x.what());return 0;}
   catch(...){copy_text(error,error_cap,"CHAOS_QUERY_UNKNOWN_EXCEPTION");return 0;}
}
int surge_chaos_cluster_get(void* opaque,const char* id,char* output,size_t output_cap,size_t* required,char* error,size_t error_cap) {
 return surge_chaos_cluster_get_bounded(opaque,id,10000,10000,1000,1000,1000,output,output_cap,required,error,error_cap);
}

int surge_retention_journal_commit(void* opaque,const char* tx,const char* idem,
    uint64_t expected,const SurgeJournalEventInput* inputs,size_t count,
    int* duplicate,uint64_t* version,char* error,size_t cap) {
 try {
  if(!opaque||!tx||!*tx||!idem||!*idem||!inputs||count!=1||
     std::strlen(tx)>256||std::strlen(idem)>256) {
   copy_text(error,cap,"RETENTION_JOURNAL_ARGUMENT_INVALID");return 0;
  }
  std::vector<surge::JournalEvent> events;
  events.reserve(count);
  for(size_t i=0;i<count;++i) {
   const auto& in=inputs[i];
   if(!in.event_id||!*in.event_id||!in.kind||!in.payload||
      std::strlen(in.event_id)>256||std::strlen(in.kind)>96||
      std::strlen(in.payload)>4*1024*1024) {
    copy_text(error,cap,"RETENTION_JOURNAL_EVENT_INVALID");return 0;
   }
   const std::string kind(in.kind);
   if(kind!="RETENTION_MANIFEST_RECORDED"&&
      kind!="RETENTION_TOMBSTONE_PROPOSED") {
    copy_text(error,cap,"RETENTION_JOURNAL_EVENT_KIND_FORBIDDEN");return 0;
   }
   events.push_back({in.event_id,kind,in.payload});
  }
  auto* handle=static_cast<Handle*>(opaque);
  auto result=handle->journal.commit(tx,idem,expected,events);
  if(duplicate)*duplicate=result.duplicate?1:0;
  if(version)*version=result.state_version;
  copy_text(error,cap,result.error);
  return result.committed?1:0;
 } catch(const std::exception& x){copy_text(error,cap,x.what());return 0;}
   catch(...){copy_text(error,cap,"RETENTION_JOURNAL_UNKNOWN_EXCEPTION");return 0;}
}
int surge_retention_journal_get_events(void* opaque,char* output,size_t output_cap,
    size_t* required,char* error,size_t error_cap) {
 try {
  if(!opaque){copy_text(error,error_cap,"RETENTION_JOURNAL_HANDLE_REQUIRED");return 0;}
  const auto events=static_cast<Handle*>(opaque)->journal.committed_events();
  std::string value="[";
  bool first=true;
  for(const auto& event:events) {
   if(event.kind!="RETENTION_MANIFEST_RECORDED"&&
      event.kind!="RETENTION_TOMBSTONE_PROPOSED")continue;
   if(!first)value+=",";
   first=false;
   value+="{\"event_id\":"+json_quote(event.event_id)+
     ",\"kind\":"+json_quote(event.kind)+
     ",\"payload\":"+json_quote(event.payload)+"}";
   if(value.size()>16*1024*1024){copy_text(error,error_cap,"RETENTION_JOURNAL_RESPONSE_TOO_LARGE");return 0;}
  }
  value+="]";
  if(required)*required=value.size()+1;
  if(!output||output_cap<value.size()+1){copy_text(error,error_cap,"RETENTION_JOURNAL_BUFFER_TOO_SMALL");return 0;}
  copy_text(output,output_cap,value);copy_text(error,error_cap,"");return 1;
 } catch(const std::exception& x){copy_text(error,error_cap,x.what());return 0;}
   catch(...){copy_text(error,error_cap,"RETENTION_JOURNAL_READ_UNKNOWN_EXCEPTION");return 0;}
}

}
