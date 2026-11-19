#include "surge/snapshot_coordinator.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)

class MemoryDomain final : public surge::SnapshotDomain {
 public:
  MemoryDomain(std::string id, std::string value) : id_(std::move(id)), value_(std::move(value)) {}
  std::string domain_id() const override { return id_; }
  std::string schema_id() const override { return id_ + ".v1"; }
  bool capture(std::string* payload, std::string*) const override {
    *payload = value_; return true;
  }
  bool capture_chunks(const surge::SnapshotChunkWriter& writer,
                      std::string* error) const override {
    for (std::size_t offset = 0; offset < value_.size();) {
      const std::size_t amount = value_.size() - offset < 3
          ? value_.size() - offset : 3;
      if (!writer(value_.data() + offset, amount, error)) return false;
      offset += amount;
    }
    if (error) error->clear();
    return true;
  }
  class Prepared final : public surge::PreparedSnapshotRestore {
   public:
    Prepared(MemoryDomain* owner, std::string value) : owner_(owner), value_(std::move(value)) {}
    void commit() noexcept override { owner_->value_ = std::move(value_); }
   private: MemoryDomain* owner_; std::string value_;
  };
  std::unique_ptr<surge::PreparedSnapshotRestore> prepare_restore(
      const std::string& payload, std::string* error) override {
    if (reject_) { if (error) *error = "TEST_REJECTED"; return nullptr; }
    return std::make_unique<Prepared>(this, payload);
  }
  std::string value() const { return value_; }
  void set_value(std::string value) { value_ = std::move(value); }
  void reject(bool value) { reject_ = value; }
 private:
  std::string id_, value_;
  bool reject_{false};
};

int main() {
  using namespace surge;
  MemoryDomain graph("graph", "graph-state"), chaos("chaos", "chaos-state");
  SnapshotCoordinator coordinator;
  std::string error, bytes;
  CHECK(coordinator.register_domain(&graph, &error));
  CHECK(coordinator.register_domain(&chaos, &error));
  CHECK(!coordinator.register_domain(&graph, &error));
  CHECK(coordinator.capture("ctx-a", 7, 19, &bytes, &error));
  graph.set_value("mutated-graph");
  chaos.set_value("mutated-chaos");
  CHECK(coordinator.restore(bytes, "ctx-a", 7, 19, &error));
  CHECK(graph.value() == "graph-state");
  CHECK(chaos.value() == "chaos-state");

  graph.set_value("keep-graph");
  chaos.set_value("keep-chaos");
  chaos.reject(true);
  CHECK(!coordinator.restore(bytes, "ctx-a", 7, 19, &error));
  CHECK(graph.value() == "keep-graph");
  CHECK(chaos.value() == "keep-chaos");
  chaos.reject(false);
  CHECK(!coordinator.restore(bytes, "wrong-context", 7, 19, &error));
  CHECK(!coordinator.restore(bytes, "ctx-a", 8, 19, &error));
  CHECK(!coordinator.restore(bytes, "ctx-a", 7, 20, &error));

  const std::string valid_snapshot_bytes = bytes;

  // Capture refuses a bundle whose payload + bundle + encoded envelope peak
  // would exceed the configured logical staging ceiling.
  SnapshotCoordinatorLimits capture_limits;
  capture_limits.max_capture_staging_bytes = 32;
  SnapshotCoordinator capture_guard(capture_limits);
  MemoryDomain capture_graph("graph", "graph-state");
  MemoryDomain capture_chaos("chaos", "chaos-state");
  CHECK(capture_guard.register_domain(&capture_graph, &error));
  CHECK(capture_guard.register_domain(&capture_chaos, &error));
  CHECK(!capture_guard.capture("ctx-a", 7, 19, &bytes, &error));
  CHECK(error == "SNAPSHOT_CAPTURE_STAGING_LIMIT_EXCEEDED");

  // Shared transient budget rejects restore before domain publication and
  // releases every reservation on both rejection and successful completion.
  ResourceBudgetLimits tiny_limits;
  tiny_limits.transient_bytes = 1;
  ResourceBudget tiny_budget(tiny_limits);
  SnapshotCoordinator guarded({}, &tiny_budget);
  MemoryDomain guarded_graph("graph", "preserve-graph");
  MemoryDomain guarded_chaos("chaos", "preserve-chaos");
  CHECK(guarded.register_domain(&guarded_graph, &error));
  CHECK(guarded.register_domain(&guarded_chaos, &error));
  CHECK(!guarded.restore(valid_snapshot_bytes, "ctx-a", 7, 19, &error));
  CHECK(error == "RESOURCE_BUDGET_EXCEEDED");
  CHECK(guarded_graph.value() == "preserve-graph");
  CHECK(guarded_chaos.value() == "preserve-chaos");
  CHECK(tiny_budget.snapshot().used[static_cast<std::size_t>(
      ResourceKind::TransientBytes)] == 0);

  ResourceBudgetLimits sufficient_limits;
  sufficient_limits.transient_bytes = valid_snapshot_bytes.size() * 3;
  ResourceBudget sufficient_budget(sufficient_limits);
  SnapshotCoordinator budgeted({}, &sufficient_budget);
  MemoryDomain budgeted_graph("graph", "old-graph");
  MemoryDomain budgeted_chaos("chaos", "old-chaos");
  CHECK(budgeted.register_domain(&budgeted_graph, &error));
  CHECK(budgeted.register_domain(&budgeted_chaos, &error));
  CHECK(budgeted.restore(valid_snapshot_bytes, "ctx-a", 7, 19, &error));
  CHECK(budgeted_graph.value() == "graph-state");
  CHECK(budgeted_chaos.value() == "chaos-state");
  CHECK(sufficient_budget.snapshot().used[static_cast<std::size_t>(
      ResourceKind::TransientBytes)] == 0);

  std::cout << "SURGE snapshot coordinator tests passed\n";
  return 0;
}
