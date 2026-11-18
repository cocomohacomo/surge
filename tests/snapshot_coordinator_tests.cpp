#include "surge/snapshot_coordinator.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)

class MemoryDomain final : public surge::SnapshotDomain {
 public:
  MemoryDomain(std::string id, std::string value) : id_(std::move(id)), value_(std::move(value)) {}
  std::string domain_id() const override { return id_; }
  std::string schema_id() const override { return id_ + ".v1"; }
  bool capture(std::string* payload, std::string*) const override {
    *payload = value_; return true;
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
  std::cout << "SURGE snapshot coordinator tests passed\n";
  return 0;
}
