#include "surge/snapshot_domains.hpp"
#include <exception>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <set>
#include <utility>
namespace surge { namespace {
bool fail(std::string* e,const char* m){if(e)*e=m;return false;}
constexpr std::size_t kMaxIds=100000,kMaxRevisions=1000000;
std::string encode_history(const std::map<std::string,std::vector<EvidenceProvenanceRecord>>& h){
 std::ostringstream o;o<<"EPH1 "<<h.size()<<' ';
 for(const auto& x:h){o<<std::quoted(x.first)<<' '<<x.second.size()<<' ';
  for(const auto& r:x.second){const auto&p=r.provenance;o<<std::quoted(p.evidence_id)<<' '<<p.revision<<' '<<r.recorded_state_version<<' ';
   auto f=[&](const std::optional<std::string>&v){o<<(v?1:0)<<' ';if(v)o<<std::quoted(*v)<<' ';};
   f(p.observed_at);f(p.source_published_at);f(p.source_retrieved_at);
  }
 }return o.str();
}
bool decode_history(const std::string& payload,std::map<std::string,std::vector<EvidenceProvenanceRecord>>& output){
 std::istringstream in(payload);std::string magic,extra;std::size_t ids=0,total=0;
 if(!(in>>magic>>ids)||magic!="EPH1"||ids>kMaxIds)return false;
 std::map<std::string,std::vector<EvidenceProvenanceRecord>> candidate;
 for(std::size_t i=0;i<ids;++i){std::string key;std::size_t n=0;
  if(!(in>>std::quoted(key)>>n)||key.empty()||n>kMaxRevisions||n>kMaxRevisions-total||candidate.count(key))return false;
  total+=n;std::vector<EvidenceProvenanceRecord> records;records.reserve(n);std::uint64_t previous=0;
  for(std::size_t j=0;j<n;++j){EvidenceProvenanceRecord r;auto&p=r.provenance;
   if(!(in>>std::quoted(p.evidence_id)>>p.revision>>r.recorded_state_version)||p.evidence_id!=key||p.revision!=j+1||!r.recorded_state_version||r.recorded_state_version<=previous)return false;
   auto f=[&](std::optional<std::string>&v){int present=0;if(!(in>>present)||(present!=0&&present!=1))return false;if(!present){v.reset();return true;}std::string s;if(!(in>>std::quoted(s))||!is_valid_rfc3339_timestamp(s))return false;v=std::move(s);return true;};
   if(!f(p.observed_at)||!f(p.source_published_at)||!f(p.source_retrieved_at))return false;
   previous=r.recorded_state_version;records.push_back(std::move(r));
  }candidate.emplace(std::move(key),std::move(records));
 }
 if (in >> extra) { return false; }
 output.swap(candidate);
 return true;
}


bool decode_provenance_event(const std::string& payload,
                             EvidenceProvenanceRecord& record) {
  std::istringstream in(payload);
  auto& p = record.provenance;
  if (!(in >> std::quoted(p.evidence_id) >> p.revision >>
        record.recorded_state_version) || p.evidence_id.empty() ||
      p.revision == 0 || record.recorded_state_version == 0) return false;
  auto read_optional = [&in](std::optional<std::string>& value) {
    int present = 0;
    if (!(in >> present) || (present != 0 && present != 1)) return false;
    if (!present) { value.reset(); return true; }
    std::string timestamp;
    if (!(in >> std::quoted(timestamp)) ||
        !is_valid_rfc3339_timestamp(timestamp)) return false;
    value = std::move(timestamp);
    return true;
  };
  if (!read_optional(p.observed_at) ||
      !read_optional(p.source_published_at) ||
      !read_optional(p.source_retrieved_at)) return false;
  std::string extra;
  return !(in >> extra);
}

bool write_strings(std::ostream& out, const std::vector<std::string>& values) {
  out << values.size() << ' ';
  for (const auto& value : values) out << std::quoted(value) << ' ';
  return static_cast<bool>(out);
}
bool read_strings(std::istream& in, std::vector<std::string>& values,
                  std::size_t maximum) {
  std::size_t count = 0;
  if (!(in >> count) || count > maximum) return false;
  values.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::string value;
    if (!(in >> std::quoted(value)) || value.empty()) return false;
    values.push_back(std::move(value));
  }
  return true;
}
std::string encode_boundary(
    const std::map<std::string, TemporalBoundaryPending>& pending) {
  std::ostringstream out;
  out << "TBL1 " << pending.size() << ' ';
  for (const auto& item : pending) {
    const auto& p = item.second;
    out << std::quoted(item.first) << ' ' << std::quoted(p.claim_id) << ' '
        << std::quoted(p.valid_from) << ' ' << std::quoted(p.valid_until) << ' '
        << std::quoted(p.status) << ' ';
    write_strings(out, p.evidence_ids);
    write_strings(out, p.packet_ids);
    out << p.candidates.size() << ' ';
    for (const auto& c : p.candidates) {
      out << std::quoted(c.candidate_id) << ' ' << std::quoted(c.role) << ' '
          << std::quoted(c.valid_from) << ' ' << std::quoted(c.valid_until) << ' '
          << std::quoted(c.evidence_id) << ' ' << std::quoted(c.packet_id) << ' '
          << (c.selected ? 1 : 0) << ' ';
    }
    auto write_map = [&out](const std::map<std::string, std::string>& values) {
      out << values.size() << ' ';
      for (const auto& value : values)
        out << std::quoted(value.first) << ' ' << std::quoted(value.second) << ' ';
    };
    write_map(p.selected_candidate_ids);
    write_map(p.selected_through_candidate_ids);
  }
  return out.str();
}
bool decode_boundary(const std::string& payload,
    std::size_t maximum_claims,
    std::map<std::string, TemporalBoundaryPending>& output) {
  std::istringstream in(payload);
  std::string magic, extra;
  std::size_t count = 0;
  if (!(in >> magic >> count) || magic != "TBL1" || count > maximum_claims)
    return false;
  std::map<std::string, TemporalBoundaryPending> candidate;
  for (std::size_t i = 0; i < count; ++i) {
    std::string key;
    TemporalBoundaryPending p;
    if (!(in >> std::quoted(key) >> std::quoted(p.claim_id) >>
          std::quoted(p.valid_from) >> std::quoted(p.valid_until) >>
          std::quoted(p.status)) || key.empty() || key != p.claim_id ||
        (p.status != "BOUNDARY_CONFLICT_REVIEW_REQUIRED" &&
         p.status != "NEEDS_OTHER_BOUNDARY" &&
         p.status != "BOUNDARIES_COMPLETE_PENDING_REVIEW" &&
         p.status != "CONFLICT_RESOLVED_PENDING_APPROVAL")) return false;
    if (!read_strings(in, p.evidence_ids, 100000) ||
        !read_strings(in, p.packet_ids, 100000)) return false;
    std::size_t candidates = 0;
    if (!(in >> candidates) || candidates > 100000) return false;
    std::set<std::string> candidate_ids;
    p.candidates.reserve(candidates);
    for (std::size_t j = 0; j < candidates; ++j) {
      TemporalBoundaryCandidate c;
      int selected = 0;
      if (!(in >> std::quoted(c.candidate_id) >> std::quoted(c.role) >>
            std::quoted(c.valid_from) >> std::quoted(c.valid_until) >>
            std::quoted(c.evidence_id) >> std::quoted(c.packet_id) >> selected) ||
          c.candidate_id.empty() || !candidate_ids.insert(c.candidate_id).second ||
          (c.role != "VALID_FROM" && c.role != "VALID_UNTIL" && c.role != "INTERVAL") ||
          c.evidence_id.empty() || c.packet_id.empty() ||
          (selected != 0 && selected != 1)) return false;
      c.selected = selected == 1;
      p.candidates.push_back(std::move(c));
    }
    auto read_map = [&in](std::map<std::string, std::string>& values) {
      std::size_t n = 0;
      if (!(in >> n) || n > 100000) return false;
      for (std::size_t x = 0; x < n; ++x) {
        std::string key, value;
        if (!(in >> std::quoted(key) >> std::quoted(value)) ||
            key.empty() || value.empty() || !values.emplace(key, value).second)
          return false;
      }
      return true;
    };
    if (!read_map(p.selected_candidate_ids) ||
        !read_map(p.selected_through_candidate_ids)) return false;
    const std::set<std::string> evidence_ids(p.evidence_ids.begin(), p.evidence_ids.end());
    const std::set<std::string> packet_ids(p.packet_ids.begin(), p.packet_ids.end());
    if (evidence_ids.size() != p.evidence_ids.size() ||
        packet_ids.size() != p.packet_ids.size()) return false;
    std::map<std::string, std::set<std::string>> ids_by_role;
    for (const auto& c : p.candidates) {
      if (!evidence_ids.count(c.evidence_id) || !packet_ids.count(c.packet_id))
        return false;
      if ((c.role == "VALID_FROM" && (c.valid_from.empty() || !c.valid_until.empty())) ||
          (c.role == "VALID_UNTIL" && (!c.valid_from.empty() || c.valid_until.empty())) ||
          (c.role == "INTERVAL" && (c.valid_from.empty() || c.valid_until.empty())))
        return false;
      ids_by_role[c.role].insert(c.candidate_id);
    }
    for (const auto& selected : p.selected_candidate_ids) {
      if (!ids_by_role[selected.first].count(selected.second)) return false;
    }
    for (const auto& selected : p.selected_through_candidate_ids) {
      if (!ids_by_role[selected.first].count(selected.second)) return false;
    }
    for (const auto& c : p.candidates) {
      const auto selected = p.selected_candidate_ids.find(c.role);
      const bool expected_selected = selected != p.selected_candidate_ids.end()
          ? selected->second == c.candidate_id
          : ids_by_role[c.role].size() == 1;
      if (c.selected != expected_selected) return false;
    }
    if (!candidate.emplace(key, std::move(p)).second) return false;
  }
  if (in >> extra) return false;
  output.swap(candidate);
  return true;
}
std::string encode_questions(
    const std::map<std::string, TemporalQuestion>& questions) {
  std::ostringstream out;
  out << "TQL1 " << questions.size() << ' ';
  for (const auto& item : questions) {
    const auto& q = item.second;
    out << std::quoted(item.first) << ' ' << std::quoted(q.question_id) << ' '
        << std::quoted(q.claim_id) << ' ' << std::quoted(q.role) << ' '
        << std::quoted(q.status) << ' ' << std::quoted(q.last_event_id) << ' ';
    write_strings(out, q.candidate_ids);
    write_strings(out, q.evidence_ids);
    write_strings(out, q.packet_ids);
  }
  return out.str();
}
bool decode_questions(const std::string& payload, std::size_t maximum_questions,
    std::map<std::string, TemporalQuestion>& output) {
  std::istringstream in(payload);
  std::string magic, extra;
  std::size_t count = 0;
  if (!(in >> magic >> count) || magic != "TQL1" || count > maximum_questions)
    return false;
  std::map<std::string, TemporalQuestion> candidate;
  for (std::size_t i = 0; i < count; ++i) {
    std::string key;
    TemporalQuestion q;
    if (!(in >> std::quoted(key) >> std::quoted(q.question_id) >>
          std::quoted(q.claim_id) >> std::quoted(q.role) >>
          std::quoted(q.status) >> std::quoted(q.last_event_id)) ||
        key.empty() || key != q.question_id || q.claim_id.empty() ||
        q.role.empty() || q.last_event_id.empty() ||
        q.question_id != "question/temporal/" + q.claim_id + "/" + q.role ||
        (q.status != "OPEN" && q.status != "UNDER_REVIEW" &&
         q.status != "RESOLVED_PENDING_APPROVAL" && q.status != "REOPENED"))
      return false;
    if (!read_strings(in, q.candidate_ids, 4096) ||
        !read_strings(in, q.evidence_ids, 8192) ||
        !read_strings(in, q.packet_ids, 8192)) return false;
    const std::set<std::string> candidate_ids(q.candidate_ids.begin(), q.candidate_ids.end());
    const std::set<std::string> evidence_ids(q.evidence_ids.begin(), q.evidence_ids.end());
    const std::set<std::string> packet_ids(q.packet_ids.begin(), q.packet_ids.end());
    if (candidate_ids.size() != q.candidate_ids.size() ||
        evidence_ids.size() != q.evidence_ids.size() ||
        packet_ids.size() != q.packet_ids.size() ||
        !candidate.emplace(key, std::move(q)).second) return false;
  }
  if (in >> extra) return false;
  output.swap(candidate);
  return true;
}
}  // namespace
class PreparedEvidenceSnapshotRestore final:public PreparedSnapshotRestore{
 public:PreparedEvidenceSnapshotRestore(EvidenceProvenanceLedger*o,std::map<std::string,std::vector<EvidenceProvenanceRecord>>c):owner(o),candidate(std::move(c)){}
 void commit()noexcept override{owner->history_.swap(candidate);}
 private:EvidenceProvenanceLedger*owner;std::map<std::string,std::vector<EvidenceProvenanceRecord>>candidate;
};
class PreparedChaosSnapshotRestore final:public PreparedSnapshotRestore{
 public:PreparedChaosSnapshotRestore(ChaosClusterLedger*o,ChaosClusterLedger::State c):owner(o),candidate(std::move(c)){}
 void commit()noexcept override{try{std::lock_guard<std::mutex> lock(owner->mutex_);owner->state_.swap(candidate);}catch(...){std::terminate();}}
 private:ChaosClusterLedger*owner;ChaosClusterLedger::State candidate;
};

class PreparedTemporalBoundarySnapshotRestore final : public PreparedSnapshotRestore {
 public:
  PreparedTemporalBoundarySnapshotRestore(TemporalBoundaryLedger* owner,
      std::map<std::string, TemporalBoundaryPending> candidate)
      : owner_(owner), candidate_(std::move(candidate)) {}
  void commit() noexcept override { owner_->pending_.swap(candidate_); }
 private:
  TemporalBoundaryLedger* owner_;
  std::map<std::string, TemporalBoundaryPending> candidate_;
};
class PreparedTemporalQuestionSnapshotRestore final : public PreparedSnapshotRestore {
 public:
  PreparedTemporalQuestionSnapshotRestore(TemporalQuestionLedger* owner,
      std::map<std::string, TemporalQuestion> candidate)
      : owner_(owner), candidate_(std::move(candidate)) {}
  void commit() noexcept override { owner_->questions_.swap(candidate_); }
 private:
  TemporalQuestionLedger* owner_;
  std::map<std::string, TemporalQuestion> candidate_;
};
std::string EvidenceProvenanceSnapshotDomain::domain_id()const{return "evidence_provenance";}
std::string EvidenceProvenanceSnapshotDomain::schema_id()const{return "surge.evidence-provenance.v1";}
bool EvidenceProvenanceSnapshotDomain::capture(std::string*p,std::string*e)const{if(!p)return fail(e,"SNAPSHOT_OUTPUT_REQUIRED");*p=encode_history(ledger_.history_);if(e)e->clear();return true;}
std::unique_ptr<PreparedSnapshotRestore> EvidenceProvenanceSnapshotDomain::prepare_restore(const std::string&p,std::string*e){
 return prepare_restore_with_tail(p, {}, e);
}
std::unique_ptr<PreparedSnapshotRestore>
EvidenceProvenanceSnapshotDomain::prepare_restore_with_tail(
    const std::string& payload, const std::vector<JournalEvent>& tail,
    std::string* error) {
 std::map<std::string,std::vector<EvidenceProvenanceRecord>> candidate;
 if(!decode_history(payload,candidate)){fail(error,"SNAPSHOT_EVIDENCE_PROVENANCE_INVALID");return nullptr;}
 std::uint64_t latest_recorded_version = 0;
 for (const auto& item : candidate)
  for (const auto& record : item.second)
   latest_recorded_version = std::max(latest_recorded_version, record.recorded_state_version);
 for(const auto& event:tail){
  if(event.kind!="EVIDENCE_PROVENANCE_SET_V1")continue;
  EvidenceProvenanceRecord record;
  if(!decode_provenance_event(event.payload,record) ||
     record.recorded_state_version!=event.state_version ||
     record.recorded_state_version==0){
   fail(error,"SNAPSHOT_EVIDENCE_TAIL_EVENT_INVALID");return nullptr;
  }
  auto& revisions=candidate[record.provenance.evidence_id];
  if(record.provenance.revision!=revisions.size()+1 ||
     record.recorded_state_version <= latest_recorded_version ||
     (!revisions.empty() && record.recorded_state_version<=revisions.back().recorded_state_version)){
   fail(error,"SNAPSHOT_EVIDENCE_TAIL_REVISION_INVALID");return nullptr;
  }
  latest_recorded_version = record.recorded_state_version;
  revisions.push_back(std::move(record));
 }
 if(error)error->clear();
 return std::make_unique<PreparedEvidenceSnapshotRestore>(&ledger_,std::move(candidate));
}
std::string ChaosClusterSnapshotDomain::domain_id()const{return "chaos_cluster";}
std::string ChaosClusterSnapshotDomain::schema_id()const{return "surge.chaos-cluster.native-v1";}
bool ChaosClusterSnapshotDomain::capture(std::string*p,std::string*e)const{
 if (!p) { return fail(e, "SNAPSHOT_OUTPUT_REQUIRED"); }
 std::lock_guard<std::mutex> lock(ledger_.mutex_);
 *p = ChaosClusterLedger::encode(ledger_.state_);
 if (e) { e->clear(); }
 return true;
}
std::unique_ptr<PreparedSnapshotRestore> ChaosClusterSnapshotDomain::prepare_restore(const std::string&p,std::string*e){
 return prepare_restore_with_tail(p, {}, e);
}
std::unique_ptr<PreparedSnapshotRestore>
ChaosClusterSnapshotDomain::prepare_restore_with_tail(
    const std::string& payload, const std::vector<JournalEvent>& tail,
    std::string* error) {
 ChaosClusterLedger::State candidate;
 if(!ChaosClusterLedger::decode(payload,candidate)){fail(error,"SNAPSHOT_CHAOS_CLUSTER_INVALID");return nullptr;}
 for(const auto&x:candidate)if(x.first.empty()||x.first!=x.second.cluster_id){fail(error,"SNAPSHOT_CHAOS_CLUSTER_ID_MISMATCH");return nullptr;}
 for(const auto& event:tail){
  if(event.kind!="CHAOS_CLUSTER_SNAPSHOT")continue;
  ChaosClusterLedger::State next;
  if(!ChaosClusterLedger::decode(event.payload,next)){
   fail(error,"SNAPSHOT_CHAOS_TAIL_EVENT_INVALID");return nullptr;
  }
  for(const auto& item:next){
   if(item.first.empty()||item.first!=item.second.cluster_id ||
      item.second.state_version!=event.state_version){
    fail(error,"SNAPSHOT_CHAOS_TAIL_WATERMARK_INVALID");return nullptr;
   }
  }
  candidate.swap(next);
 }
 if(error)error->clear();
 return std::make_unique<PreparedChaosSnapshotRestore>(&ledger_,std::move(candidate));
}

std::string TemporalBoundarySnapshotDomain::domain_id() const {
  return "temporal_boundary";
}
std::string TemporalBoundarySnapshotDomain::schema_id() const {
  return "surge.temporal-boundary.v1";
}
bool TemporalBoundarySnapshotDomain::capture(std::string* payload,
                                              std::string* error) const {
  if (!payload) return fail(error, "SNAPSHOT_OUTPUT_REQUIRED");
  *payload = encode_boundary(ledger_.pending_);
  if (error) error->clear();
  return true;
}
std::unique_ptr<PreparedSnapshotRestore>
TemporalBoundarySnapshotDomain::prepare_restore(const std::string& payload,
                                                 std::string* error) {
  return prepare_restore_with_tail(payload, {}, error);
}
std::unique_ptr<PreparedSnapshotRestore>
TemporalBoundarySnapshotDomain::prepare_restore_with_tail(
    const std::string& payload, const std::vector<JournalEvent>& tail,
    std::string* error) {
  std::map<std::string, TemporalBoundaryPending> candidate;
  if (!decode_boundary(payload, ledger_.max_claims_, candidate)) {
    fail(error, "SNAPSHOT_TEMPORAL_BOUNDARY_INVALID");
    return nullptr;
  }
  TemporalBoundaryLedger staged(ledger_.journal_, ledger_.max_claims_);
  staged.pending_ = std::move(candidate);
  for (const auto& event : tail) {
    if (event.kind != "TEMPORAL_BOUNDARY_CONTRIBUTION_V1" &&
        event.kind != "TEMPORAL_BOUNDARY_CLEARED_V1" &&
        event.kind != "TEMPORAL_BOUNDARY_CANDIDATE_SELECTED_V1") continue;
    if (!staged.apply_event(event)) {
      fail(error, "SNAPSHOT_TEMPORAL_BOUNDARY_TAIL_EVENT_INVALID");
      return nullptr;
    }
  }
  if (error) error->clear();
  return std::make_unique<PreparedTemporalBoundarySnapshotRestore>(
      &ledger_, std::move(staged.pending_));
}
std::string TemporalQuestionSnapshotDomain::domain_id() const {
  return "temporal_question";
}
std::string TemporalQuestionSnapshotDomain::schema_id() const {
  return "surge.temporal-question.v1";
}
bool TemporalQuestionSnapshotDomain::capture(std::string* payload,
                                              std::string* error) const {
  if (!payload) return fail(error, "SNAPSHOT_OUTPUT_REQUIRED");
  *payload = encode_questions(ledger_.questions_);
  if (error) error->clear();
  return true;
}
std::unique_ptr<PreparedSnapshotRestore>
TemporalQuestionSnapshotDomain::prepare_restore(const std::string& payload,
                                                 std::string* error) {
  return prepare_restore_with_tail(payload, {}, error);
}
std::unique_ptr<PreparedSnapshotRestore>
TemporalQuestionSnapshotDomain::prepare_restore_with_tail(
    const std::string& payload, const std::vector<JournalEvent>& tail,
    std::string* error) {
  std::map<std::string, TemporalQuestion> candidate;
  if (!decode_questions(payload, ledger_.max_questions_, candidate)) {
    fail(error, "SNAPSHOT_TEMPORAL_QUESTION_INVALID");
    return nullptr;
  }
  TemporalQuestionLedger staged(ledger_.journal_, ledger_.max_questions_);
  staged.questions_ = std::move(candidate);
  for (const auto& event : tail) {
    if (event.kind != "TEMPORAL_QUESTION_SNAPSHOT_V1") continue;
    if (!staged.apply_event(event)) {
      fail(error, "SNAPSHOT_TEMPORAL_QUESTION_TAIL_EVENT_INVALID");
      return nullptr;
    }
  }
  if (error) error->clear();
  return std::make_unique<PreparedTemporalQuestionSnapshotRestore>(
      &ledger_, std::move(staged.questions_));
}
} // namespace surge
