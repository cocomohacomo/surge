#include "surge/evidence_provenance.hpp"
#include <cassert>
#include <filesystem>
using namespace surge;

int main() {
  const auto path = (std::filesystem::temp_directory_path() /
                     "surge-evidence-provenance.sgj").string();
  std::filesystem::remove(path);
  {
    EvidenceProvenanceLedger ledger(path);
    assert(ledger.open());
    EvidenceProvenance first;
    first.evidence_id = "evidence-1";
    first.revision = 1;
    first.observed_at = "2026-11-17T09:00:00+09:00";
    first.source_published_at = "2026-11-17T00:00:00Z";
    first.source_retrieved_at = "2026-11-17T09:05:00+09:00";
    EvidenceProvenanceRecord stored;
    std::string error;
    assert(ledger.append(first, 0, stored, error));
    assert(stored.recorded_state_version == 1);
    assert(ledger.latest("evidence-1")->provenance.observed_at ==
           first.observed_at);

    EvidenceProvenance invalid = first;
    invalid.revision = 2;
    invalid.observed_at = "2026-02-30T12:00:00Z";
    assert(!ledger.append(invalid, 1, stored, error));
    assert(error == "EVIDENCE_PROVENANCE_TIMESTAMP_INVALID");
    assert(ledger.state_version() == 1);

    EvidenceProvenance correction;
    correction.evidence_id = "evidence-1";
    correction.revision = 2;
    correction.observed_at = "2026-11-17T08:58:00Z";
    correction.source_published_at.reset();
    correction.source_retrieved_at = "2026-11-17T09:10:00+09:00";
    assert(ledger.append(correction, 1, stored, error));
    assert(stored.recorded_state_version == 2);
    assert(ledger.history().at("evidence-1").size() == 2);
    assert(!ledger.append(correction, 1, stored, error));
    assert(error == "STATE_VERSION_CONFLICT");
  }
  {
    EvidenceProvenanceLedger recovered(path);
    assert(recovered.open());
    assert(recovered.state_version() == 2);
    assert(recovered.history().at("evidence-1").size() == 2);
    const auto latest = recovered.latest("evidence-1");
    assert(latest && latest->provenance.revision == 2);
    assert(!latest->provenance.source_published_at);
  }
  assert(is_valid_rfc3339_timestamp("2024-02-29T23:59:59Z"));
  assert(!is_valid_rfc3339_timestamp("2023-02-29T23:59:59Z"));
  assert(!is_valid_rfc3339_timestamp("2026-11-17T09:00:00"));
  std::filesystem::remove(path);
}
