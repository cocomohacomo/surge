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
    std::string error;
    assert(guarded.recover(error));

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

  const std::string question_path = "budgeted_context_graph_question_test.journal";
  std::remove(question_path.c_str());
  {
    EventJournal journal(question_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    ResourceBudgetLimits limits;
    limits.resident_bytes = 8192;
    limits.transient_bytes = 8192;
    limits.retained_events = 1;
    limits.retained_clusters = 4;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(graph, budget, 8192);
    std::string error;
    assert(guarded.recover(error));

    SemanticDelta create;
    create.delta_id = "question-create";
    create.idempotency_key = "question-create-key";
    create.expected_state_version = graph.state_version();
    create.nodes.push_back({{"q1", NodeKind::Question, "question", {}}});
    assert(guarded.apply(create).committed);

    assert(guarded.set_question_memory_state("q1", QuestionMemoryState::Dormant,
        graph.state_version(), error));
    assert(graph.question_memory_state("q1") == QuestionMemoryState::Dormant);
    const auto after_first = budget.snapshot();
    assert(after_first.used[static_cast<std::size_t>(ResourceKind::RetainedEvents)] == 1);

    error.clear();
    assert(!guarded.set_question_memory_state("q1", QuestionMemoryState::Active,
        graph.state_version(), error));
    assert(error == "RESOURCE_BUDGET_EXCEEDED");
    assert(graph.question_memory_state("q1") == QuestionMemoryState::Dormant);
  }
  std::remove(question_path.c_str());
  return 0;
}
