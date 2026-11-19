#include "test_support.hpp"
#include "surge/budgeted_packet_admission.hpp"
#include "surge/sha256.hpp"

#include <filesystem>

using namespace surge;

int main() {
  const auto root = std::filesystem::temp_directory_path() / "surge-budgeted-admission";
  std::filesystem::remove_all(root);
  const auto path = (root / "events.sgj").string();
  {
    EventJournal journal((root / "over-limit.sgj").string());
    CHECK(journal.open());
    ResourceBudget budget({697, 4096, 1, 4, 16, 64});
    BudgetedPacketAdmissionCoordinator admission(journal, budget);
    std::string error;
    CHECK(admission.recover(error));
    ValidatedRecognitionPacket p;
    p.packet_id = "p1"; p.idempotency_key = "i1"; p.context_id = "ctx";
    p.source_id = "doc"; p.source_generation = "g1"; p.block_ref = "b1";
    p.offset_basis = "UTF8_BYTE_NFC"; p.start_offset = 0; p.end_offset = 3;
    p.source_sequence = 1; p.source_snapshot_digest = sha256_hex("abc");
    p.source_range_digest = sha256_hex("abc"); p.canonical_request_digest = "canonical-p1";
    const auto rejected = admission.admit(p, "abc");
    CHECK(!rejected.admitted && rejected.error == "PACKET_RESIDENT_RESOURCE_BUDGET_EXCEEDED");
    CHECK(journal.state_version() == 0);
    CHECK(admission.retained_packet_count() == 0);
    CHECK(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::ResidentBytes)] == 0);
  }
  {
    EventJournal journal(path);
    CHECK(journal.open());
    ResourceBudget budget({698, 4096, 1, 4, 16, 64});
    BudgetedPacketAdmissionCoordinator admission(journal, budget);
    std::string error;
    CHECK(admission.recover(error));
    ValidatedRecognitionPacket p;
    p.packet_id = "p1"; p.idempotency_key = "i1"; p.context_id = "ctx";
    p.source_id = "doc"; p.source_generation = "g1"; p.block_ref = "b1";
    p.offset_basis = "UTF8_BYTE_NFC"; p.start_offset = 0; p.end_offset = 3;
    p.source_sequence = 1; p.source_snapshot_digest = sha256_hex("abc");
    p.source_range_digest = sha256_hex("abc"); p.canonical_request_digest = "canonical-p1";

    auto admitted = admission.admit(p, "abc");
    CHECK(admitted.admitted && !admitted.duplicate);
    CHECK(admission.retained_packet_count() == 1);
    const auto snapshot = budget.snapshot();
    CHECK(snapshot.used[static_cast<std::size_t>(ResourceKind::ResidentBytes)] > 0);
    CHECK(snapshot.used[static_cast<std::size_t>(ResourceKind::RetainedEvents)] == 1);

    auto duplicate = admission.admit(p, "abc");
    CHECK(duplicate.admitted && duplicate.duplicate);
    CHECK(admission.retained_packet_count() == 1);
    CHECK(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::RetainedEvents)] == 1);
  }
  {
    EventJournal journal(path);
    CHECK(journal.open());
    ResourceBudget budget({698, 4096, 1, 4, 16, 64});
    BudgetedPacketAdmissionCoordinator admission(journal, budget);
    std::string error;
    CHECK(admission.recover(error));
    CHECK(admission.retained_packet_count() == 1);
    CHECK(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::RetainedEvents)] == 1);
  }
  std::filesystem::remove_all(root);
  return 0;
}
