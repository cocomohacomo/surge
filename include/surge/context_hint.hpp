#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "surge/event_journal.hpp"

namespace surge {
enum class HintClass {
  OpenScope, RequiredQuestionAspect, TriggeredDependency, RequiredLineage,
  ActiveFocusScope, RelatedCluster, HistoricalAnalogue, LowPriority
};
struct HintCandidate {
  std::string ref;
  HintClass kind{HintClass::LowPriority};
  unsigned source_overlap{0};
  unsigned dependency_depth{0};
};
struct RetrievalTicket {
  std::string ticket_id;
  std::string context_id;
  std::string packet_ref;
  std::string source_range_digest;
  std::string omitted_digest;
  std::string policy_version;
  std::uint64_t state_version{0};
  std::vector<std::string> omitted_refs;
};
struct ContextHint {
  std::uint64_t state_version{0};
  bool context_complete{true};
  std::vector<std::string> included_refs;
  std::vector<std::string> omitted_refs;
  std::map<HintClass,std::size_t> omitted_by_class;
  std::string included_digest;
  std::string omitted_digest;
  std::string retrieval_ticket_id;
};
struct HintBuildResult {
  ContextHint hint;
  bool ticket_required{false};
  RetrievalTicket ticket;
  std::string error;
};
enum class TicketStatus { Issued, Resolved, Stale };
struct TicketResolution {
  TicketStatus status{TicketStatus::Stale};
  std::uint64_t resolved_state_version{0};
  std::vector<std::string> refs;
};
class ContextHintBuilder {
 public:
  static HintBuildResult build(const std::string& context_id,const std::string& packet_ref,
      const std::string& source_range_digest,const std::string& policy_version,
      std::uint64_t state_version,const std::vector<HintCandidate>& candidates,
      std::size_t reference_budget);
};
class RetrievalTicketRegistry {
 public:
  bool issue(const RetrievalTicket& ticket);
  TicketResolution resolve(const std::string& ticket_id,std::uint64_t current_state_version,
      bool query_replayable,const std::vector<std::string>& reevaluated_refs={});
  std::size_t size() const { return tickets_.size(); }
 private:
  std::map<std::string,RetrievalTicket> tickets_;
  std::map<std::string,TicketResolution> resolutions_;
};

// Journal-backed ticket registry. The EventJournal is shared with the Core so
// ticket issue/resolve operations participate in the same durable version order.
class PersistentRetrievalTicketRegistry {
 public:
  explicit PersistentRetrievalTicketRegistry(EventJournal& journal) : journal_(journal) {}
  bool open();
  bool issue(const RetrievalTicket& ticket, std::string& error);
  TicketResolution resolve(const std::string& ticket_id, std::uint64_t current_state_version,
      bool query_replayable, const std::vector<std::string>& reevaluated_refs,
      std::string& error);
  std::size_t size() const { return tickets_.size(); }
 private:
  bool replay();
  EventJournal& journal_;
  std::map<std::string, RetrievalTicket> tickets_;
  std::map<std::string, TicketResolution> resolutions_;
};
}  // namespace surge
