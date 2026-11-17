#pragma once
#include "surge/event_journal.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace surge {
struct TemporalBoundaryContribution { std::string claim_id,role,valid_from,valid_until,evidence_id,packet_id; };
struct TemporalBoundaryCandidate { std::string candidate_id,role,valid_from,valid_until,evidence_id,packet_id; bool selected{false}; };
struct TemporalBoundaryPending {
 std::string claim_id,valid_from,valid_until,status;
 std::vector<std::string> evidence_ids,packet_ids;
 std::vector<TemporalBoundaryCandidate> candidates;
 std::map<std::string,std::string> selected_candidate_ids;
 std::map<std::string,std::string> selected_through_candidate_ids;
};
struct TemporalBoundaryLedgerResult {
 bool committed{false},duplicate{false}; std::uint64_t state_version{0};
 std::string status,error; TemporalBoundaryPending pending;
};
class TemporalBoundaryLedger {
 public:
 explicit TemporalBoundaryLedger(EventJournal&,std::size_t max_claims=4096);
 bool replay();
 TemporalBoundaryLedgerResult add(const std::string&,const std::string&,const std::string&,std::uint64_t,const TemporalBoundaryContribution&);
 TemporalBoundaryLedgerResult resolve_candidate(const std::string&,const std::string&,const std::string&,std::uint64_t,const std::string&,const std::string&,const std::string&);
 TemporalBoundaryLedgerResult clear(const std::string&,const std::string&,const std::string&,std::uint64_t,const std::string&);
 bool get(const std::string&,TemporalBoundaryPending&) const;
 std::size_t pending_claim_count() const; std::string last_error() const;
 private:
 bool apply_event(const JournalEvent&);
 bool validate_contribution(const TemporalBoundaryContribution&,std::string&) const;
 void refresh_pending(TemporalBoundaryPending&) const;
 TemporalBoundaryLedgerResult result_for(const std::string&,const JournalCommitResult&) const;
 EventJournal& journal_; const std::size_t max_claims_;
 std::map<std::string,TemporalBoundaryPending> pending_; std::string last_error_;
};
} // namespace surge
