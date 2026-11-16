#include "surge/temporal_boundary_ledger.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace surge {
namespace {
constexpr char kContributionKind[] = "TEMPORAL_BOUNDARY_CONTRIBUTION_V1";
constexpr char kClearKind[] = "TEMPORAL_BOUNDARY_CLEARED_V1";

std::string encode_contribution(const TemporalBoundaryContribution& value) {
  std::ostringstream out;
  out << "1 " << std::quoted(value.claim_id) << ' ' << std::quoted(value.role)
      << ' ' << std::quoted(value.valid_from) << ' ' << std::quoted(value.valid_until)
      << ' ' << std::quoted(value.evidence_id) << ' ' << std::quoted(value.packet_id);
  return out.str();
}

bool decode_contribution(const std::string& payload, TemporalBoundaryContribution& value) {
  std::istringstream in(payload);
  int version = 0;
  if (!(in >> version >> std::quoted(value.claim_id) >> std::quoted(value.role)
        >> std::quoted(value.valid_from) >> std::quoted(value.valid_until)
        >> std::quoted(value.evidence_id) >> std::quoted(value.packet_id)) || version != 1)
    return false;
  in >> std::ws;
  return in.eof();
}

std::string encode_clear(const std::string& claim_id) {
  std::ostringstream out;
  out << "1 " << std::quoted(claim_id);
  return out.str();
}

bool decode_clear(const std::string& payload, std::string& claim_id) {
  std::istringstream in(payload);
  int version = 0;
  if (!(in >> version >> std::quoted(claim_id)) || version != 1 || claim_id.empty())
    return false;
  in >> std::ws;
  return in.eof();
}

void append_unique(std::vector<std::string>& values, const std::string& value) {
  for (const auto& existing : values) if (existing == value) return;
  values.push_back(value);
}
}  // namespace

TemporalBoundaryLedger::TemporalBoundaryLedger(EventJournal& journal, std::size_t max_claims)
    : journal_(journal), max_claims_(max_claims) {}

bool TemporalBoundaryLedger::replay() {
  pending_.clear();
  last_error_.clear();
  if (max_claims_ == 0) {
    last_error_ = "TEMPORAL_LEDGER_CAPACITY_INVALID";
    return false;
  }
  for (const auto& event : journal_.committed_events()) {
    if (event.kind == kContributionKind || event.kind == kClearKind) {
      if (!apply_event(event)) {
        last_error_ = "TEMPORAL_LEDGER_EVENT_INVALID:" + event.event_id;
        pending_.clear();
        return false;
      }
    }
  }
  return true;
}

bool TemporalBoundaryLedger::validate_contribution(
    const TemporalBoundaryContribution& contribution, std::string& status) const {
  if (contribution.claim_id.empty()) { status = "CLAIM_ID_REQUIRED"; return false; }
  if (contribution.evidence_id.empty() || contribution.packet_id.empty()) {
    status = "PROVENANCE_REQUIRED"; return false;
  }
  const auto& role = contribution.role;
  if (role != "VALID_FROM" && role != "VALID_UNTIL" && role != "INTERVAL") {
    status = "UNSUPPORTED_TEMPORAL_ROLE"; return false;
  }
  if (role == "VALID_FROM" &&
      (contribution.valid_from.empty() || !contribution.valid_until.empty())) {
    status = "CANDIDATE_ROLE_BOUNDARY_MISMATCH"; return false;
  }
  if (role == "VALID_UNTIL" &&
      (!contribution.valid_from.empty() || contribution.valid_until.empty())) {
    status = "CANDIDATE_ROLE_BOUNDARY_MISMATCH"; return false;
  }
  if (role == "INTERVAL" &&
      (contribution.valid_from.empty() || contribution.valid_until.empty())) {
    status = "CANDIDATE_INTERVAL_INCOMPLETE"; return false;
  }
  const auto found = pending_.find(contribution.claim_id);
  if (found == pending_.end() && pending_.size() >= max_claims_) {
    status = "ACCUMULATOR_CAPACITY_EXCEEDED"; return false;
  }
  if (found != pending_.end()) {
    const auto& current = found->second;
    if (role == "INTERVAL" &&
        (!current.valid_from.empty() || !current.valid_until.empty())) {
      status = "BOUNDARY_CONFLICT_REVIEW_REQUIRED"; return false;
    }
    if (role == "VALID_FROM" && !current.valid_from.empty() &&
        current.valid_from != contribution.valid_from) {
      status = "BOUNDARY_CONFLICT_REVIEW_REQUIRED"; return false;
    }
    if (role == "VALID_UNTIL" && !current.valid_until.empty() &&
        current.valid_until != contribution.valid_until) {
      status = "BOUNDARY_CONFLICT_REVIEW_REQUIRED"; return false;
    }
    if (role != "INTERVAL" &&
        ((!contribution.valid_from.empty() && !current.valid_until.empty() &&
          contribution.valid_from >= current.valid_until) ||
         (!contribution.valid_until.empty() && !current.valid_from.empty() &&
          contribution.valid_until <= current.valid_from))) {
      status = "INTERVAL_INVALID_REVIEW_REQUIRED"; return false;
    }
  }
  status = "ACCEPTED";
  return true;
}

bool TemporalBoundaryLedger::apply_event(const JournalEvent& event) {
  if (event.kind == kClearKind) {
    std::string claim_id;
    if (!decode_clear(event.payload, claim_id)) return false;
    pending_.erase(claim_id);
    return true;
  }
  TemporalBoundaryContribution contribution;
  if (!decode_contribution(event.payload, contribution)) return false;
  std::string status;
  if (!validate_contribution(contribution, status)) return false;
  auto inserted = pending_.emplace(contribution.claim_id,
      TemporalBoundaryPending{contribution.claim_id, {}, {}, {}, {}});
  auto& current = inserted.first->second;
  if (contribution.role == "VALID_FROM" || contribution.role == "INTERVAL")
    current.valid_from = contribution.valid_from;
  if (contribution.role == "VALID_UNTIL" || contribution.role == "INTERVAL")
    current.valid_until = contribution.valid_until;
  append_unique(current.evidence_ids, contribution.evidence_id);
  append_unique(current.packet_ids, contribution.packet_id);
  return true;
}

TemporalBoundaryLedgerResult TemporalBoundaryLedger::result_for(
    const std::string& claim_id, const JournalCommitResult& commit) const {
  TemporalBoundaryLedgerResult result;
  result.committed = commit.committed;
  result.duplicate = commit.duplicate;
  result.state_version = commit.state_version;
  result.error = commit.error;
  const auto found = pending_.find(claim_id);
  if (found != pending_.end()) {
    result.pending = found->second;
    result.status = found->second.valid_from.empty() || found->second.valid_until.empty()
        ? "NEEDS_OTHER_BOUNDARY" : "BOUNDARIES_COMPLETE_PENDING_REVIEW";
  } else {
    result.pending.claim_id = claim_id;
    result.status = "CLEARED";
  }
  return result;
}

TemporalBoundaryLedgerResult TemporalBoundaryLedger::add(
    const std::string& transaction_id, const std::string& idempotency_key,
    const std::string& event_id, std::uint64_t expected_state_version,
    const TemporalBoundaryContribution& contribution) {
  std::string status;
  if (!validate_contribution(contribution, status)) {
    TemporalBoundaryLedgerResult result;
    result.state_version = journal_.state_version();
    result.status = status;
    result.error = status;
    return result;
  }
  JournalEvent event{event_id, kContributionKind, encode_contribution(contribution)};
  const auto committed = journal_.commit(transaction_id, idempotency_key,
                                          expected_state_version, {event});
  if (committed.committed && !committed.duplicate && !apply_event(event)) {
    last_error_ = "TEMPORAL_LEDGER_APPLY_AFTER_COMMIT_FAILED";
    TemporalBoundaryLedgerResult result;
    result.state_version = committed.state_version;
    result.status = "COMMITTED_REPLAY_REQUIRED";
    result.error = last_error_;
    return result;
  }
  return result_for(contribution.claim_id, committed);
}

TemporalBoundaryLedgerResult TemporalBoundaryLedger::clear(
    const std::string& transaction_id, const std::string& idempotency_key,
    const std::string& event_id, std::uint64_t expected_state_version,
    const std::string& claim_id) {
  if (claim_id.empty()) {
    TemporalBoundaryLedgerResult result;
    result.state_version = journal_.state_version();
    result.status = "CLAIM_ID_REQUIRED";
    result.error = result.status;
    return result;
  }
  JournalEvent event{event_id, kClearKind, encode_clear(claim_id)};
  const auto committed = journal_.commit(transaction_id, idempotency_key,
                                          expected_state_version, {event});
  if (committed.committed && !committed.duplicate) pending_.erase(claim_id);
  return result_for(claim_id, committed);
}

bool TemporalBoundaryLedger::get(const std::string& claim_id,
                                 TemporalBoundaryPending& output) const {
  const auto found = pending_.find(claim_id);
  if (found == pending_.end()) return false;
  output = found->second;
  return true;
}

std::size_t TemporalBoundaryLedger::pending_claim_count() const { return pending_.size(); }
std::string TemporalBoundaryLedger::last_error() const { return last_error_; }

}  // namespace surge
