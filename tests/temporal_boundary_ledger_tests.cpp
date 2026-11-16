#include "surge/temporal_boundary_ledger.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using namespace surge;

int main() {
  const auto root = std::filesystem::temp_directory_path() / "surge-temporal-ledger-tests";
  std::filesystem::remove_all(root);
  const auto path = (root / "events.sgj").string();
  {
    EventJournal journal(path);
    assert(journal.open());
    TemporalBoundaryLedger ledger(journal);
    assert(ledger.replay());
    auto first = ledger.add("tx-1", "idem-1", "event-1", 0,
        {"claim/A", "VALID_FROM", "2026-04-01T00:00:00+09:00", "",
         "evidence/1", "packet/1"});
    assert(first.committed && first.status == "NEEDS_OTHER_BOUNDARY");
    auto second = ledger.add("tx-2", "idem-2", "event-2", 1,
        {"claim/A", "VALID_UNTIL", "", "2026-05-01T00:00:00+09:00",
         "evidence/2", "packet/2"});
    assert(second.committed && second.status == "BOUNDARIES_COMPLETE_PENDING_REVIEW");
    assert(second.pending.evidence_ids.size() == 2);
    assert(second.pending.packet_ids.size() == 2);
    assert(!second.pending.valid_from.empty() && !second.pending.valid_until.empty());
    assert(ledger.pending_claim_count() == 1);
  }
  {
    EventJournal recovered(path);
    assert(recovered.open());
    TemporalBoundaryLedger ledger(recovered);
    assert(ledger.replay());
    TemporalBoundaryPending restored;
    assert(ledger.get("claim/A", restored));
    assert(restored.valid_from == "2026-04-01T00:00:00+09:00");
    assert(restored.valid_until == "2026-05-01T00:00:00+09:00");
    assert(restored.evidence_ids.size() == 2 && restored.packet_ids.size() == 2);
    auto conflict = ledger.add("tx-3", "idem-3", "event-3", 2,
        {"claim/A", "VALID_FROM", "2026-03-01T00:00:00+09:00", "",
         "evidence/3", "packet/3"});
    assert(!conflict.committed && conflict.status == "BOUNDARY_CONFLICT_REVIEW_REQUIRED");
    auto cleared = ledger.clear("tx-3", "idem-3", "event-3", 2, "claim/A");
    assert(cleared.committed && cleared.status == "CLEARED");
    assert(ledger.pending_claim_count() == 0);
  }
  {
    EventJournal recovered(path);
    assert(recovered.open());
    TemporalBoundaryLedger ledger(recovered);
    assert(ledger.replay());
    assert(ledger.pending_claim_count() == 0);
  }
  {
    EventJournal journal((root / "bounded.sgj").string());
    assert(journal.open());
    TemporalBoundaryLedger ledger(journal, 1);
    assert(ledger.replay());
    auto a = ledger.add("tx-a", "idem-a", "event-a", 0,
        {"claim/A", "VALID_FROM", "2026-04-01T00:00:00+09:00", "", "e1", "p1"});
    assert(a.committed);
    auto b = ledger.add("tx-b", "idem-b", "event-b", 1,
        {"claim/B", "VALID_FROM", "2026-04-01T00:00:00+09:00", "", "e2", "p2"});
    assert(!b.committed && b.status == "ACCUMULATOR_CAPACITY_EXCEEDED");
  }
  {
    EventJournal journal((root / "invalid.sgj").string());
    assert(journal.open());
    TemporalBoundaryLedger ledger(journal);
    assert(ledger.replay());
    auto invalid = ledger.add("tx-invalid", "idem-invalid", "event-invalid", 0,
        {"claim/A", "VALID_FROM", "", "", "e1", "p1"});
    assert(!invalid.committed && invalid.status == "CANDIDATE_ROLE_BOUNDARY_MISMATCH");
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE temporal boundary ledger tests: 5 groups passed\n";
}
