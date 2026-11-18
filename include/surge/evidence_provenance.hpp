#pragma once
#include "surge/event_journal.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace surge {

// Source provenance clocks are independent from modeled valid time and journal time.
struct EvidenceProvenance {
  std::string evidence_id;
  std::optional<std::string> observed_at;
  std::optional<std::string> source_published_at;
  std::optional<std::string> source_retrieved_at;
  std::uint64_t revision{0};
};

struct EvidenceProvenanceRecord {
  EvidenceProvenance provenance;
  std::uint64_t recorded_state_version{0};
};

class EvidenceProvenanceSnapshotDomain;
class EvidenceProvenanceLedger {
 public:
  explicit EvidenceProvenanceLedger(std::string journal_path);
  explicit EvidenceProvenanceLedger(EventJournal& shared_journal);
  bool open();
  bool append(const EvidenceProvenance& provenance,
              std::uint64_t expected_state_version,
              EvidenceProvenanceRecord& result,
              std::string& error);
  bool prepare_event(const EvidenceProvenance& provenance,
                     std::uint64_t expected_state_version,
                     JournalEvent& event, std::string& error) const;
  bool replay();
  std::uint64_t state_version() const;
  std::optional<EvidenceProvenanceRecord> latest(const std::string& evidence_id) const;
  const std::map<std::string, std::vector<EvidenceProvenanceRecord>>& history() const {
    return history_;
  }

 private:
  friend class EvidenceProvenanceSnapshotDomain;
  bool apply(const EvidenceProvenanceRecord&, std::string& error);
  std::unique_ptr<EventJournal> owned_journal_;
  EventJournal* journal_{nullptr};
  std::map<std::string, std::vector<EvidenceProvenanceRecord>> history_;
};

bool is_valid_rfc3339_timestamp(const std::string& value);

}  // namespace surge
