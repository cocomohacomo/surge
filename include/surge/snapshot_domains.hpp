#pragma once
#include "surge/chaos_cluster.hpp"
#include "surge/context_graph.hpp"
#include "surge/evidence_provenance.hpp"
#include "surge/snapshot_coordinator.hpp"
#include "surge/temporal_boundary_ledger.hpp"
#include "surge/temporal_question_ledger.hpp"
namespace surge {
class ContextGraphSnapshotDomain final : public SnapshotDomain {
 public:
  explicit ContextGraphSnapshotDomain(ContextGraph& graph) : graph_(graph) {}
  std::string domain_id() const override;
  std::string schema_id() const override;
  bool capture(std::string*, std::string*) const override;
  std::unique_ptr<PreparedSnapshotRestore> prepare_restore(const std::string&, std::string*) override;
  std::unique_ptr<PreparedSnapshotRestore> prepare_restore_with_tail(const std::string&, const std::vector<JournalEvent>&, std::string*) override;
 private: ContextGraph& graph_;
};
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
class TemporalBoundarySnapshotDomain final : public SnapshotDomain {
 public:
  explicit TemporalBoundarySnapshotDomain(TemporalBoundaryLedger& ledger) : ledger_(ledger) {}
  std::string domain_id() const override;
  std::string schema_id() const override;
  bool capture(std::string*, std::string*) const override;
  std::unique_ptr<PreparedSnapshotRestore> prepare_restore(const std::string&, std::string*) override;
 private: TemporalBoundaryLedger& ledger_;
};
class TemporalQuestionSnapshotDomain final : public SnapshotDomain {
 public:
  explicit TemporalQuestionSnapshotDomain(TemporalQuestionLedger& ledger) : ledger_(ledger) {}
  std::string domain_id() const override;
  std::string schema_id() const override;
  bool capture(std::string*, std::string*) const override;
  std::unique_ptr<PreparedSnapshotRestore> prepare_restore(const std::string&, std::string*) override;
 private: TemporalQuestionLedger& ledger_;
};
}  // namespace surge
