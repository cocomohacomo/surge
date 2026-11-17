#include "test_support.hpp"
#include "surge/evidence_provenance.hpp"
#include <filesystem>
using namespace surge;

int main() {
  const auto path = (std::filesystem::temp_directory_path() /
                     "surge-evidence-provenance.sgj").string();
  std::filesystem::remove(path);
  {
    EvidenceProvenanceLedger ledger(path);
    CHECK(ledger.open());
    EvidenceProvenance first;
    first.evidence_id = "evidence-1";
    first.revision = 1;
    first.observed_at = "2026-11-17T09:00:00+09:00";
    first.source_published_at = "2026-11-17T00:00:00Z";
    first.source_retrieved_at = "2026-11-17T09:05:00+09:00";
    EvidenceProvenanceRecord stored;
    std::string error;
    CHECK(ledger.append(first, 0, stored, error));
    CHECK(stored.recorded_state_version == 1);
    CHECK(ledger.latest("evidence-1")->provenance.observed_at ==
           first.observed_at);

    EvidenceProvenance invalid = first;
    invalid.revision = 2;
    invalid.observed_at = "2026-02-30T12:00:00Z";
    CHECK(!ledger.append(invalid, 1, stored, error));
    CHECK(error == "EVIDENCE_PROVENANCE_TIMESTAMP_INVALID");
    CHECK(ledger.state_version() == 1);

    EvidenceProvenance correction;
    correction.evidence_id = "evidence-1";
    correction.revision = 2;
    correction.observed_at = "2026-11-17T08:58:00Z";
    correction.source_published_at.reset();
    correction.source_retrieved_at = "2026-11-17T09:10:00+09:00";
    CHECK(ledger.append(correction, 1, stored, error));
    CHECK(stored.recorded_state_version == 2);
    CHECK(ledger.history().at("evidence-1").size() == 2);
    CHECK(!ledger.append(correction, 1, stored, error));
    CHECK(error == "STATE_VERSION_CONFLICT");
  }
  {
    EvidenceProvenanceLedger recovered(path);
    CHECK(recovered.open());
    CHECK(recovered.state_version() == 2);
    CHECK(recovered.history().at("evidence-1").size() == 2);
    const auto latest = recovered.latest("evidence-1");
    CHECK(latest && latest->provenance.revision == 2);
    CHECK(!latest->provenance.source_published_at);
  }
  CHECK(is_valid_rfc3339_timestamp("2024-02-29T23:59:59Z"));
  CHECK(!is_valid_rfc3339_timestamp("2023-02-29T23:59:59Z"));
  CHECK(!is_valid_rfc3339_timestamp("2026-11-17T09:00:00"));
  std::filesystem::remove(path);
}
