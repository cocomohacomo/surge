#include "test_support.hpp"
#include "surge/temporal_boundary_ledger.hpp"

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
    CHECK(journal.open());
    TemporalBoundaryLedger ledger(journal);
    CHECK(ledger.replay());
    auto first = ledger.add("tx-1", "idem-1", "event-1", 0,
        {"claim/A", "VALID_FROM", "2026-04-01T00:00:00+09:00", "",
         "evidence/1", "packet/1"});
    CHECK(first.committed && first.status == "NEEDS_OTHER_BOUNDARY");
    auto second = ledger.add("tx-2", "idem-2", "event-2", 1,
        {"claim/A", "VALID_UNTIL", "", "2026-05-01T00:00:00+09:00",
         "evidence/2", "packet/2"});
    CHECK(second.committed && second.status == "BOUNDARIES_COMPLETE_PENDING_REVIEW");
    CHECK(second.pending.evidence_ids.size() == 2);
    CHECK(second.pending.packet_ids.size() == 2);
    CHECK(!second.pending.valid_from.empty() && !second.pending.valid_until.empty());
    CHECK(ledger.pending_claim_count() == 1);
  }
  {
    EventJournal journal((root / "interval-idempotency.sgj").string());
    CHECK(journal.open());
    TemporalBoundaryLedger ledger(journal);
    CHECK(ledger.replay());
    const TemporalBoundaryContribution interval{
        "claim/interval", "INTERVAL",
        "2026-04-01T00:00:00+09:00", "2027-04-01T00:00:00+09:00",
        "evidence/interval", "packet/interval"};
    auto first = ledger.add("tx-interval", "idem-interval", "event-interval", 0, interval);
    CHECK(first.committed && first.status == "BOUNDARIES_COMPLETE_PENDING_REVIEW");
    auto retry = ledger.add("tx-interval", "idem-interval", "event-interval", 0, interval);
    CHECK(retry.committed && retry.duplicate);
    CHECK(retry.pending.valid_from == interval.valid_from);
    CHECK(retry.pending.valid_until == interval.valid_until);
  }
  {
    EventJournal recovered(path);
    CHECK(recovered.open());
    TemporalBoundaryLedger ledger(recovered);
    CHECK(ledger.replay());
    TemporalBoundaryPending restored;
    CHECK(ledger.get("claim/A", restored));
    CHECK(restored.valid_from == "2026-04-01T00:00:00+09:00");
    CHECK(restored.valid_until == "2026-05-01T00:00:00+09:00");
    CHECK(restored.evidence_ids.size() == 2 && restored.packet_ids.size() == 2);
    auto conflict = ledger.add("tx-3", "idem-3", "event-3", 2,
        {"claim/A", "VALID_FROM", "2026-03-01T00:00:00+09:00", "",
         "evidence/3", "packet/3"});
    CHECK(conflict.committed && conflict.status == "BOUNDARY_CONFLICT_REVIEW_REQUIRED");
    CHECK(conflict.pending.candidates.size() == 3);
    auto resolved = ledger.resolve_candidate("tx-4", "idem-4", "event-4", 3,
        "claim/A", "VALID_FROM", "event-1");
    CHECK(resolved.committed && resolved.status == "CONFLICT_RESOLVED_PENDING_APPROVAL");
    CHECK(resolved.pending.valid_from == "2026-04-01T00:00:00+09:00");
    auto later_conflict = ledger.add("tx-5", "idem-5", "event-5", 4,
        {"claim/A", "VALID_FROM", "2026-02-15T00:00:00+09:00", "",
         "evidence/4", "packet/4"});
    CHECK(later_conflict.committed &&
          later_conflict.status == "BOUNDARY_CONFLICT_REVIEW_REQUIRED");
    auto cleared = ledger.clear("tx-6", "idem-6", "event-6", 5, "claim/A");
    CHECK(cleared.committed && cleared.status == "CLEARED");
    CHECK(ledger.pending_claim_count() == 0);
  }
  {
    EventJournal recovered(path);
    CHECK(recovered.open());
    TemporalBoundaryLedger ledger(recovered);
    CHECK(ledger.replay());
    CHECK(ledger.pending_claim_count() == 0);
  }
  {
    EventJournal journal((root / "bounded.sgj").string());
    CHECK(journal.open());
    TemporalBoundaryLedger ledger(journal, 1);
    CHECK(ledger.replay());
    auto a = ledger.add("tx-a", "idem-a", "event-a", 0,
        {"claim/A", "VALID_FROM", "2026-04-01T00:00:00+09:00", "", "e1", "p1"});
    CHECK(a.committed);
    auto b = ledger.add("tx-b", "idem-b", "event-b", 1,
        {"claim/B", "VALID_FROM", "2026-04-01T00:00:00+09:00", "", "e2", "p2"});
    CHECK(!b.committed && b.status == "ACCUMULATOR_CAPACITY_EXCEEDED");
  }
  {
    EventJournal journal((root / "invalid.sgj").string());
    CHECK(journal.open());
    TemporalBoundaryLedger ledger(journal);
    CHECK(ledger.replay());
    auto invalid = ledger.add("tx-invalid", "idem-invalid", "event-invalid", 0,
        {"claim/A", "VALID_FROM", "", "", "e1", "p1"});
    CHECK(!invalid.committed && invalid.status == "CANDIDATE_ROLE_BOUNDARY_MISMATCH");
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE temporal boundary ledger tests: 5 groups passed\n";
}
