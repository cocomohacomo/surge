#include "surge/core.hpp"

namespace surge {
IngestResult RangeLedger::ingest(const std::string& key, const std::string& digest,
                                 const std::string& idem, bool revision,
                                 std::uint64_t revision_sequence) {
  const std::string request = key + "|" + digest + (revision ? "|rev|" + std::to_string(revision_sequence) : "|base");
  auto k = keys_.find(idem);
  if (k != keys_.end() && k->second != request) return {false, "IDEMPOTENCY_KEY_REUSED"};
  if (k != keys_.end()) return {true, {}};
  auto it = ranges_.find(key);
  if (it != ranges_.end()) {
    const auto& latest = it->second.back();
    if (!revision) {
      if (latest.digest != digest) return {false, "RANGE_CONTENT_CONFLICT"};
      return {false, "METADATA_CONFLICT"};
    }
    if (revision_sequence == 0 || revision_sequence <= latest.revision_sequence || latest.digest == digest)
      return {false, "SOURCE_REVISION_INVALID"};
    ++revision_count_;
    it->second.push_back({digest, idem, request, revision_sequence});
    keys_[idem] = request;
    ++evidence_count_;
    return {false, {}};
  }
  if (revision) return {false, "REVISION_TARGET_NOT_FOUND"};
  ranges_[key].push_back({digest, idem, request, 0});
  keys_[idem] = request;
  ++evidence_count_;
  return {false, {}};
}

void SourceSequencer::observe(std::uint64_t sequence) {
  if (sequence <= watermark_) {
    auto g = gaps_.find(sequence);
    if (g != gaps_.end() && g->second.state == GapState::AuthorizedSkip) {
      packets_.insert(sequence); g->second.state = GapState::Reopened;
    }
    advance(); return;
  }
  packets_.insert(sequence);
  for (std::uint64_t s=watermark_+1; s<sequence; ++s)
    if (!packets_.count(s) && !gaps_.count(s)) gaps_[s] = Gap{s, GapState::Open, {}};
  auto g=gaps_.find(sequence);
  if (g!=gaps_.end() && g->second.state==GapState::Open) g->second.state=GapState::ResolvedByPacket;
  advance();
}
bool SourceSequencer::authorize_skip(std::uint64_t seq, const std::string& capability,
                                     const std::string& reason, const std::string& policy,
                                     const std::string& key) {
  auto g=gaps_.find(seq);
  const std::string req=std::to_string(seq)+"|"+capability+"|"+reason+"|"+policy;
  auto old=skip_requests_.find(key);
  if(old!=skip_requests_.end()) return old->second==req;
  if (capability!="SOURCE_GAP_SKIP" || reason.empty() || policy.empty() || key.empty() ||
      g==gaps_.end() || g->second.state!=GapState::Open) return false;
  skip_requests_[key]=req; g->second.state=GapState::AuthorizedSkip;
  g->second.skip_key=key; ++audit_count_; advance(); return true;
}
bool SourceSequencer::ingest_late(std::uint64_t seq) {
  auto g=gaps_.find(seq);
  if(g==gaps_.end() || g->second.state!=GapState::AuthorizedSkip) return false;
  packets_.insert(seq); g->second.state=GapState::Reopened; advance(); return true;
}
void SourceSequencer::advance() {
  for (;;) {
    const auto next=watermark_+1;
    if(packets_.count(next)) { ++watermark_; continue; }
    auto g=gaps_.find(next);
    if(g!=gaps_.end() && g->second.state==GapState::AuthorizedSkip) { ++watermark_; continue; }
    break;
  }
}
std::string SourceSequencer::packet_status(std::uint64_t seq) const {
  if(seq<=watermark_) return "COMMITTED";
  auto g=gaps_.find(seq);
  if(g!=gaps_.end() && g->second.state==GapState::Open) return "BLOCKED_BY_GAP";
  if(packets_.count(seq)) return seq>watermark_+1 ? "READY_BUT_BLOCKED" : "READY";
  return "UNKNOWN";
}
bool ScopeSpan::close(bool opener, bool closer, bool target) {
  if ((state_!=ScopeState::Open && state_!=ScopeState::Unresolved) || !opener || !closer || !target) return false;
  state_=ScopeState::Closed; return true;
}
void ScopeSpan::mark_unresolved() { if(state_==ScopeState::Open) state_=ScopeState::Unresolved; }
bool ScopeSpan::invalidate(const std::string& ref) {
  if(ref.empty() || state_==ScopeState::Invalidated) return false;
  revisions_.push_back(ref); state_=ScopeState::Invalidated; return true;
}
TicketResult resolve_ticket(std::uint64_t ticket, std::uint64_t current, bool replayable) {
  if(ticket==current) return {true,false,current};
  if(replayable) return {true,false,current};
  return {false,true,current};
}
Gate quality_gate(std::size_t eligible, std::size_t accepted, double minimum) {
  if(eligible==0 || accepted>eligible || minimum<0 || minimum>1) return Gate::Inconclusive;
  return static_cast<double>(accepted)/eligible >= minimum ? Gate::Pass : Gate::Fail;
}
Gate performance_gate(double measured, double maximum) {
  if(measured<0 || maximum<=0) return Gate::Inconclusive;
  return measured<=maximum ? Gate::Pass : Gate::Fail;
}
Gate overall_gate(Gate semantic, Gate performance, std::size_t violations) {
  if(violations) return Gate::Fail;
  if(semantic==Gate::Fail || performance==Gate::Fail) return Gate::Fail;
  if(semantic==Gate::Inconclusive || performance==Gate::Inconclusive) return Gate::Inconclusive;
  return Gate::Pass;
}
}  // namespace surge
