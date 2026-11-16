#pragma once
#include "surge/event_journal.hpp"
#include "surge/temporal_boundary_ledger.hpp"
#include <map>
#include <string>
#include <vector>
namespace surge {
struct TemporalQuestion {
 std::string question_id,claim_id,role,status,last_event_id;
 std::vector<std::string> candidate_ids,evidence_ids,packet_ids;
};
struct TemporalQuestionResult {
 bool committed{false},duplicate{false}; std::uint64_t state_version{0};
 std::string status,error; TemporalQuestion question;
};
class TemporalQuestionLedger {
 public:
 explicit TemporalQuestionLedger(EventJournal&,std::size_t max_questions=16384);
 bool replay();
 TemporalQuestionResult synchronize(const std::string&,const std::string&,const std::string&,
   std::uint64_t,const TemporalBoundaryPending&);
 bool get(const std::string&,const std::string&,TemporalQuestion&) const;
 std::vector<TemporalQuestion> for_claim(const std::string&) const;
 std::size_t size() const; std::string last_error() const;
 private:
 bool apply_event(const JournalEvent&);
 EventJournal& journal_; const std::size_t max_questions_;
 std::map<std::string,TemporalQuestion> questions_; std::string last_error_;
};
}
