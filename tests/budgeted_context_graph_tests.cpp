#include "surge/budgeted_context_graph.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace surge;

int main() {
  const std::string path = "budgeted_context_graph_test.journal";
  std::remove(path.c_str());
  {
    EventJournal journal(path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    ResourceBudgetLimits limits;
    limits.resident_bytes = 1024;
    limits.transient_bytes = 4096;
    limits.retained_clusters = 1;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(graph, budget, 4096);
    assert(guarded.recover(*new std::string()));

    SemanticDelta first;
    first.delta_id = "d1"; first.idempotency_key = "k1";
    first.expected_state_version = graph.state_version();
    first.nodes.push_back({{"root", NodeKind::Cluster, "root", {}}});
    auto accepted = guarded.apply(first);
    assert(accepted.committed);
    assert(graph.nodes().count("root") == 1);

    SemanticDelta second;
    second.delta_id = "d2"; second.idempotency_key = "k2";
    second.expected_state_version = graph.state_version();
    second.nodes.push_back({{"child", NodeKind::Cluster, "child", {}}});
    auto rejected = guarded.apply(second);
    assert(!rejected.committed);
    assert(rejected.error == "RESOURCE_BUDGET_EXCEEDED");
    assert(graph.nodes().count("child") == 0);
    assert(graph.state_version() == accepted.state_version);

    const auto snapshot = budget.snapshot();
    assert(snapshot.used[static_cast<std::size_t>(ResourceKind::RetainedClusters)] == 1);
    assert(guarded.accounted_resident_bytes() > 0);
  }
  std::remove(path.c_str());
  return 0;
}
