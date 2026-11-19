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

  const std::string forgetting_path = "question_forgetting_protection_test.journal";
  std::remove(forgetting_path.c_str());
  {
    EventJournal journal(forgetting_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    SemanticDelta create;
    create.delta_id = "protected-questions";
    create.idempotency_key = "protected-questions-key";
    create.expected_state_version = graph.state_version();
    create.nodes.push_back({{"focus-cluster", NodeKind::Cluster, "focused", {}}});
    create.nodes.push_back({{"q-focus", NodeKind::Question, "focused question", {}}});
    create.nodes.push_back({{"q-low", NodeKind::Question, "low priority question", {}}});
    create.relations.push_back({{"q-focus-about-focus", RelationKind::About,
        "q-focus", "focus-cluster"}});
    create.focus = SetFocus{{"focus-cluster", "", false}};
    assert(graph.apply(create).committed);

    std::vector<std::string> demoted;
    std::string error;
    const std::vector<std::pair<std::string,double>> scores{
        {"q-focus", 0.1}, {"q-low", 0.99}};
    assert(graph.enforce_question_memory_capacity(scores, 1, graph.state_version(),
        demoted, error));
    assert(demoted.size() == 1 && demoted.front() == "q-low");
    assert(graph.question_memory_state("q-focus") == QuestionMemoryState::Active);
    assert(graph.question_memory_state("q-low") == QuestionMemoryState::Dormant);
  }
  std::remove(forgetting_path.c_str());

  const std::string dependency_path = "question_dependency_protection_test.journal";
  std::remove(dependency_path.c_str());
  {
    EventJournal journal(dependency_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    SemanticDelta create;
    create.delta_id = "dependency-protected-questions";
    create.idempotency_key = "dependency-protected-questions-key";
    create.expected_state_version = graph.state_version();
    create.nodes.push_back({{"dependency-focus", NodeKind::Cluster, "focused", {}}});
    create.nodes.push_back({{"q-anchor", NodeKind::Question, "anchor", {}}});
    create.nodes.push_back({{"q-one-hop", NodeKind::Question, "one hop", {}}});
    create.nodes.push_back({{"q-two-hop", NodeKind::Question, "two hops", {}}});
    create.relations.push_back({{"anchor-about-focus", RelationKind::About,
        "q-anchor", "dependency-focus"}});
    create.relations.push_back({{"anchor-to-one", RelationKind::DependsOn,
        "q-anchor", "q-one-hop"}});
    create.relations.push_back({{"one-to-two", RelationKind::DependsOn,
        "q-one-hop", "q-two-hop"}});
    create.focus = SetFocus{{"dependency-focus", "", false}};
    assert(graph.apply(create).committed);

    std::vector<std::string> demoted;
    std::string error;
    const std::vector<std::pair<std::string,double>> scores{
        {"q-anchor", 0.1}, {"q-one-hop", 0.2}, {"q-two-hop", 0.99}};
    assert(graph.enforce_question_memory_capacity(scores, 2, graph.state_version(),
        demoted, error));
    assert(demoted.size() == 1 && demoted.front() == "q-two-hop");
    assert(graph.question_memory_state("q-anchor") == QuestionMemoryState::Active);
    assert(graph.question_memory_state("q-one-hop") == QuestionMemoryState::Active);
    assert(graph.question_memory_state("q-two-hop") == QuestionMemoryState::Dormant);
  }
  std::remove(dependency_path.c_str());
  return 0;
}
