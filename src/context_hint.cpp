#include "surge/context_hint.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace surge {
namespace {
int priority(HintClass kind){
  switch(kind){
    case HintClass::OpenScope:return 0;
    case HintClass::RequiredQuestionAspect:return 1;
    case HintClass::TriggeredDependency:return 2;
    case HintClass::RequiredLineage:return 3;
    case HintClass::ActiveFocusScope:return 4;
    case HintClass::RelatedCluster:return 5;
    case HintClass::HistoricalAnalogue:return 6;
    case HintClass::LowPriority:return 7;
  }
  return 8;
}
std::string digest_refs(const std::vector<std::string>& refs){
  // Stable non-cryptographic FNV-1a fingerprint; source authenticity is external.
  std::uint64_t h=1469598103934665603ULL;
  for(const auto& ref:refs){
    for(unsigned char c:ref){h^=c;h*=1099511628211ULL;}
    h^=0xFFU;h*=1099511628211ULL;
  }
  std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return out.str();
}
std::string ticket_key(const RetrievalTicket& t){
  return t.context_id+"|"+t.packet_ref+"|"+t.source_range_digest+"|"+
    t.omitted_digest+"|"+t.policy_version;
}
}
HintBuildResult ContextHintBuilder::build(const std::string& context,const std::string& packet,
    const std::string& range_digest,const std::string& policy,std::uint64_t version,
    const std::vector<HintCandidate>& input,std::size_t budget){
  HintBuildResult result;
  if(context.empty()||packet.empty()||range_digest.size()!=64||policy.empty()||budget==0){
    result.error="CONTEXT_HINT_REQUEST_INVALID";return result;
  }
  std::vector<HintCandidate> candidates=input;
  for(const auto& c:candidates)if(c.ref.empty()){result.error="CONTEXT_HINT_REF_INVALID";return result;}
  std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
    if(priority(a.kind)!=priority(b.kind))return priority(a.kind)<priority(b.kind);
    if(a.source_overlap!=b.source_overlap)return a.source_overlap>b.source_overlap;
    if(a.dependency_depth!=b.dependency_depth)return a.dependency_depth<b.dependency_depth;
    return a.ref<b.ref;
  });
  std::vector<HintCandidate> unique;
  std::set<std::string> seen;
  for(const auto& c:candidates)if(seen.insert(c.ref).second)unique.push_back(c);
  const auto mandatory_end=std::find_if(unique.begin(),unique.end(),[](const auto& c){return priority(c.kind)>=5;});
  const auto mandatory_count=static_cast<std::size_t>(std::distance(unique.begin(),mandatory_end));
  const bool mandatory_overflow=mandatory_count>budget;
  const std::size_t include_count=mandatory_overflow?budget:std::min(budget,unique.size());
  result.hint.state_version=version;
  for(std::size_t i=0;i<unique.size();++i){
    if(i<include_count)result.hint.included_refs.push_back(unique[i].ref);
    else{
      result.hint.omitted_refs.push_back(unique[i].ref);
      ++result.hint.omitted_by_class[unique[i].kind];
    }
  }
  result.hint.context_complete=result.hint.omitted_refs.empty();
  result.hint.included_digest=digest_refs(result.hint.included_refs);
  result.hint.omitted_digest=digest_refs(result.hint.omitted_refs);
  if(mandatory_overflow){
    result.hint.context_complete=false;
    result.ticket_required=true;
    result.ticket.context_id=context;result.ticket.packet_ref=packet;
    result.ticket.source_range_digest=range_digest;result.ticket.omitted_digest=result.hint.omitted_digest;
    result.ticket.policy_version=policy;result.ticket.state_version=version;
    for(std::size_t i=budget;i<mandatory_count;++i)
      result.ticket.omitted_refs.push_back(unique[i].ref);
    result.ticket.omitted_digest=digest_refs(result.ticket.omitted_refs);
    result.ticket.ticket_id="rt/"+digest_refs({ticket_key(result.ticket)});
    result.hint.retrieval_ticket_id=result.ticket.ticket_id;
  } else if(!result.hint.omitted_refs.empty()){
    // Optional truncation is visible but does not imply a mandatory lookup.
    result.hint.context_complete=true;
  }
  return result;
}
bool RetrievalTicketRegistry::issue(const RetrievalTicket& ticket){
  if(ticket.ticket_id.empty()||ticket.context_id.empty()||ticket.packet_ref.empty()||
     ticket.omitted_digest.empty()||ticket.policy_version.empty()||ticket.omitted_refs.empty())return false;
  const auto it=tickets_.find(ticket.ticket_id);
  if(it!=tickets_.end())return ticket_key(it->second)==ticket_key(ticket);
  tickets_.emplace(ticket.ticket_id,ticket);return true;
}
TicketResolution RetrievalTicketRegistry::resolve(const std::string& id,std::uint64_t current,
    bool replayable,const std::vector<std::string>& reevaluated){
  const auto it=tickets_.find(id);
  if(it==tickets_.end())return {TicketStatus::Stale,current,{}};
  const auto prior=resolutions_.find(id);
  if(prior!=resolutions_.end()&&prior->second.resolved_state_version==current)return prior->second;
  if(current==it->second.state_version){
    TicketResolution r{TicketStatus::Resolved,current,it->second.omitted_refs};
    resolutions_[id]=r;return r;
  }
  if(!replayable)return {TicketStatus::Stale,current,{}};
  std::vector<std::string> refs=reevaluated;
  std::sort(refs.begin(),refs.end());refs.erase(std::unique(refs.begin(),refs.end()),refs.end());
  TicketResolution r{TicketStatus::Resolved,current,std::move(refs)};
  resolutions_[id]=r;return r;
}
}  // namespace surge
