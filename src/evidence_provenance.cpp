#include "surge/evidence_provenance.hpp"
#include <iomanip>
#include <sstream>

namespace surge {
namespace {
bool digits(const std::string& s, std::size_t pos, std::size_t count, int& out) {
  if (pos + count > s.size()) return false;
  out = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const char c = s[pos + i];
    if (c < '0' || c > '9') return false;
    out = out * 10 + c - '0';
  }
  return true;
}
std::string encode(const EvidenceProvenanceRecord& r) {
  const auto& p = r.provenance;
  std::ostringstream out;
  out << std::quoted(p.evidence_id) << ' ' << p.revision << ' '
      << r.recorded_state_version << ' ';
  auto field = [&](const std::optional<std::string>& value) {
    out << (value ? 1 : 0) << ' ';
    if (value) out << std::quoted(*value) << ' ';
  };
  field(p.observed_at);
  field(p.source_published_at);
  field(p.source_retrieved_at);
  return out.str();
}
bool decode(const std::string& payload, EvidenceProvenanceRecord& r) {
  std::istringstream in(payload);
  auto& p = r.provenance;
  if (!(in >> std::quoted(p.evidence_id) >> p.revision >> r.recorded_state_version))
    return false;
  auto field = [&](std::optional<std::string>& value) {
    int present = 0;
    if (!(in >> present) || (present != 0 && present != 1)) return false;
    if (!present) { value.reset(); return true; }
    std::string text;
    if (!(in >> std::quoted(text))) return false;
    value = std::move(text);
    return true;
  };
  if (!field(p.observed_at) || !field(p.source_published_at) ||
      !field(p.source_retrieved_at)) return false;
  std::string extra;
  return !(in >> extra);
}
}  // namespace

bool is_valid_rfc3339_timestamp(const std::string& value) {
  if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
      (value[10] != 'T' && value[10] != 't') || value[13] != ':' ||
      value[16] != ':') return false;
  int year=0, month=0, day=0, hour=0, minute=0, second=0;
  if (!digits(value,0,4,year) || !digits(value,5,2,month) ||
      !digits(value,8,2,day) || !digits(value,11,2,hour) ||
      !digits(value,14,2,minute) || !digits(value,17,2,second)) return false;
  if (year == 0 || month < 1 || month > 12 || hour > 23 ||
      minute > 59 || second > 59) return false;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const int month_days[] = {31, leap ? 29 : 28, 31, 30, 31, 30,
                            31, 31, 30, 31, 30, 31};
  if (day < 1 || day > month_days[month - 1]) return false;
  std::size_t pos = 19;
  if (pos < value.size() && value[pos] == '.') {
    ++pos;
    const auto begin = pos;
    while (pos < value.size() && value[pos] >= '0' && value[pos] <= '9') ++pos;
    if (pos == begin || pos - begin > 9) return false;
  }
  if (pos < value.size() && (value[pos] == 'Z' || value[pos] == 'z'))
    return pos + 1 == value.size();
  if (pos + 6 != value.size() ||
      (value[pos] != '+' && value[pos] != '-') || value[pos + 3] != ':')
    return false;
  int offset_hour=0, offset_minute=0;
  return digits(value,pos+1,2,offset_hour) &&
         digits(value,pos+4,2,offset_minute) &&
         offset_hour <= 23 && offset_minute <= 59;
}

EvidenceProvenanceLedger::EvidenceProvenanceLedger(std::string path)
    : owned_journal_(std::make_unique<EventJournal>(std::move(path))),
      journal_(owned_journal_.get()) {}
EvidenceProvenanceLedger::EvidenceProvenanceLedger(EventJournal& journal)
    : journal_(&journal) {}

bool EvidenceProvenanceLedger::open() {
  if (!journal_ || !journal_->open()) return false;
  return replay();
}
std::uint64_t EvidenceProvenanceLedger::state_version() const {
  return journal_ ? journal_->state_version() : 0;
}
std::optional<EvidenceProvenanceRecord> EvidenceProvenanceLedger::latest(
    const std::string& id) const {
  const auto it = history_.find(id);
  if (it == history_.end() || it->second.empty()) return std::nullopt;
  return it->second.back();
}
bool EvidenceProvenanceLedger::apply(const EvidenceProvenanceRecord& record,
                                     std::string& error) {
  const auto& p = record.provenance;
  if (p.evidence_id.empty() || p.revision == 0) {
    error = "EVIDENCE_PROVENANCE_ID_OR_REVISION_INVALID"; return false;
  }
  const auto valid = [](const std::optional<std::string>& value) {
    return !value || is_valid_rfc3339_timestamp(*value);
  };
  if (!valid(p.observed_at) || !valid(p.source_published_at) ||
      !valid(p.source_retrieved_at)) {
    error = "EVIDENCE_PROVENANCE_TIMESTAMP_INVALID"; return false;
  }
  auto& revisions = history_[p.evidence_id];
  if (p.revision != revisions.size() + 1 ||
      record.recorded_state_version == 0 ||
      (!revisions.empty() &&
       record.recorded_state_version <= revisions.back().recorded_state_version)) {
    error = "EVIDENCE_PROVENANCE_REVISION_INVALID"; return false;
  }
  revisions.push_back(record);
  return true;
}
bool EvidenceProvenanceLedger::prepare_event(const EvidenceProvenance& provenance,
    std::uint64_t expected, JournalEvent& event, std::string& error) const {
  error.clear();
  if (!journal_ || expected != journal_->state_version()) {
    error = "STATE_VERSION_CONFLICT"; return false;
  }
  if (expected == UINT64_MAX) {
    error = "STATE_VERSION_EXHAUSTED"; return false;
  }
  const auto valid = [](const std::optional<std::string>& value) {
    return !value || is_valid_rfc3339_timestamp(*value);
  };
  if (provenance.evidence_id.empty() || provenance.revision == 0) {
    error = "EVIDENCE_PROVENANCE_ID_OR_REVISION_INVALID"; return false;
  }
  if (!valid(provenance.observed_at) || !valid(provenance.source_published_at) ||
      !valid(provenance.source_retrieved_at)) {
    error = "EVIDENCE_PROVENANCE_TIMESTAMP_INVALID"; return false;
  }
  const auto found = history_.find(provenance.evidence_id);
  const auto next_revision = found == history_.end() ? 1 : found->second.size() + 1;
  if (provenance.revision != next_revision) {
    error = "EVIDENCE_PROVENANCE_REVISION_INVALID"; return false;
  }
  EvidenceProvenanceRecord record{provenance, expected + 1};
  const auto event_id = "evidence-provenance:" + provenance.evidence_id + ":" +
                        std::to_string(provenance.revision);
  event = {event_id, "EVIDENCE_PROVENANCE_SET_V1", encode(record)};
  return true;
}
bool EvidenceProvenanceLedger::append(const EvidenceProvenance& provenance,
    std::uint64_t expected, EvidenceProvenanceRecord& result, std::string& error) {
  JournalEvent event;
  if (!prepare_event(provenance, expected, event, error)) return false;
  const auto committed = journal_->commit(event.event_id, event.event_id, expected, {event});
  if (!committed.committed) { error = committed.error; return false; }
  if (!replay()) { error = "EVIDENCE_PROVENANCE_REPLAY_FAILED"; return false; }
  const auto record = latest(provenance.evidence_id);
  if (!record) { error = "EVIDENCE_PROVENANCE_RECORD_MISSING"; return false; }
  result = *record;
  return true;
}
bool EvidenceProvenanceLedger::replay() {
  history_.clear();
  std::uint64_t previous_version = 0;
  for (const auto& event : journal_->committed_events()) {
    if (event.kind != "EVIDENCE_PROVENANCE_SET_V1") continue;
    EvidenceProvenanceRecord record;
    if (!decode(event.payload, record) ||
        record.recorded_state_version <= previous_version ||
        record.recorded_state_version > journal_->state_version()) return false;
    std::string error;
    if (!apply(record, error)) return false;
    previous_version = record.recorded_state_version;
  }
  return true;
}
}  // namespace surge
