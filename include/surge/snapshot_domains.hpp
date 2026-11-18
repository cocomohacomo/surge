#pragma once
#include "surge/chaos_cluster.hpp"
#include "surge/evidence_provenance.hpp"
#include "surge/snapshot_coordinator.hpp"
namespace surge {
class EvidenceProvenanceSnapshotDomain final : public SnapshotDomain {
 public:
  explicit EvidenceProvenanceSnapshotDomain(EvidenceProvenanceLedger& ledger) : ledger_(ledger) {}
  std::string domain_id() const override;
  std::string schema_id() const override;
  bool capture(std::string*, std::string*) const override;
  std::unique_ptr<PreparedSnapshotRestore> prepare_restore(const std::string&, std::string*) override;
 private: EvidenceProvenanceLedger& ledger_;
};
class ChaosClusterSnapshotDomain final : public SnapshotDomain {
 public:
  explicit ChaosClusterSnapshotDomain(ChaosClusterLedger& ledger) : ledger_(ledger) {}
  std::string domain_id() const override;
  std::string schema_id() const override;
  bool capture(std::string*, std::string*) const override;
  std::unique_ptr<PreparedSnapshotRestore> prepare_restore(const std::string&, std::string*) override;
 private: ChaosClusterLedger& ledger_;
};
}  // namespace surge
