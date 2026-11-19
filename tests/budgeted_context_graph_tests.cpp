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
    limits.retained_events = 8;
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
    limits.retained_events = 2;
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
    assert(after_first.used[static_cast<std::size_t>(ResourceKind::RetainedEvents)] == 2);

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
  const std::string classification_path = "budgeted_cluster_classification_test.journal";
  std::remove(classification_path.c_str());
  {
    EventJournal journal(classification_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    SemanticDelta seed;
    seed.delta_id = "classification-seed";
    seed.idempotency_key = "classification-seed-key";
    seed.expected_state_version = graph.state_version();
    seed.nodes.push_back({{"root", NodeKind::Cluster, "root", {}}});
    seed.nodes.push_back({{"entity-a", NodeKind::Entity, "alpha", {}}});
    seed.nodes.push_back({{"entity-b", NodeKind::Entity, "beta", {}}});
    assert(graph.apply(seed).committed);

    ResourceBudgetLimits limits;
    limits.resident_bytes = 16384;
    limits.transient_bytes = 16384;
    limits.retained_events = 64;
    limits.retained_clusters = 2;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(graph, budget, 16384);
    std::string error;
    assert(guarded.recover(error));

    SemanticDelta classification;
    classification.delta_id = "classification-a";
    classification.idempotency_key = "classification-a-key";
    classification.expected_state_version = graph.state_version();
    std::string cluster_id;
    auto accepted = guarded.append_cluster_membership(std::move(classification),
        "root", "LEXICAL_INTERPRETATION", "span:0:2", "entity-a",
        "membership-a", cluster_id, error);
    assert(accepted.committed);
    assert(!cluster_id.empty());
    assert(graph.nodes().count(cluster_id) == 1);

    const auto before_reject = graph.state_version();
    SemanticDelta rejected_delta;
    rejected_delta.delta_id = "classification-b";
    rejected_delta.idempotency_key = "classification-b-key";
    rejected_delta.expected_state_version = before_reject;
    std::string rejected_cluster_id;
    auto rejected = guarded.append_cluster_membership(std::move(rejected_delta),
        "root", "SURFACE_REALIZATION", "span:2:4", "entity-b",
        "membership-b", rejected_cluster_id, error);
    assert(!rejected.committed);
    assert(error == "RESOURCE_BUDGET_EXCEEDED");
    assert(rejected_cluster_id.empty());
    assert(graph.state_version() == before_reject);
    assert(graph.nodes().count("entity-b") == 1);
  }
  std::remove(classification_path.c_str());

  // A zero per-operation staging ceiling must not disable the shared
  // transient-memory budget. Rejection happens before event materialization
  // and leaves the graph watermark and contents unchanged.
  const std::string transient_path = "budgeted_context_graph_transient_test.journal";
  std::remove(transient_path.c_str());
  {
    EventJournal journal(transient_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    ResourceBudgetLimits limits;
    limits.resident_bytes = 8192;
    limits.transient_bytes = 1;
    limits.retained_events = 16;
    limits.retained_clusters = 4;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(graph, budget);  // no local staging ceiling
    std::string error;
    assert(guarded.recover(error));

    SemanticDelta delta;
    delta.delta_id = "transient-rejected";
    delta.idempotency_key = "transient-rejected-key";
    delta.expected_state_version = graph.state_version();
    delta.nodes.push_back({{"not-committed", NodeKind::Entity, "payload", {}}});
    const auto before = graph.state_version();
    const auto rejected = guarded.apply(delta);
    assert(!rejected.committed);
    assert(rejected.error == "RESOURCE_BUDGET_EXCEEDED");
    assert(graph.state_version() == before);
    assert(graph.nodes().count("not-committed") == 0);
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::TransientBytes)] == 0);
  }
  std::remove(transient_path.c_str());

  // The facade cannot mutate against an unaccounted pre-existing graph.
  const std::string unrecovered_path = "budgeted_context_graph_unrecovered_test.journal";
  std::remove(unrecovered_path.c_str());
  {
    EventJournal journal(unrecovered_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    ResourceBudgetLimits limits;
    limits.resident_bytes = 8192;
    limits.transient_bytes = 8192;
    limits.retained_events = 16;
    limits.retained_clusters = 4;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(graph, budget);
    SemanticDelta delta;
    delta.delta_id = "must-not-commit";
    delta.idempotency_key = "must-not-commit-key";
    delta.expected_state_version = graph.state_version();
    delta.nodes.push_back({{"unrecovered-node", NodeKind::Entity, "payload", {}}});
    const auto before = graph.state_version();
    const auto rejected = guarded.apply(delta);
    assert(!rejected.committed);
    assert(rejected.error == "CONTEXT_GRAPH_BUDGET_NOT_RECOVERED");
    assert(graph.state_version() == before);
    assert(graph.nodes().count("unrecovered-node") == 0);
  }
  std::remove(unrecovered_path.c_str());

  // Individual Question mutations also reserve transient work even when the
  // optional local staging ceiling is disabled.
  const std::string question_transient_path = "budgeted_question_transient_test.journal";
  std::remove(question_transient_path.c_str());
  {
    EventJournal journal(question_transient_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    SemanticDelta create;
    create.delta_id = "question-transient-create";
    create.idempotency_key = "question-transient-create-key";
    create.expected_state_version = graph.state_version();
    create.nodes.push_back({{"q-transient", NodeKind::Question, "question", {}}});
    assert(graph.apply(create).committed);

    ResourceBudgetLimits limits;
    limits.resident_bytes = 8192;
    limits.transient_bytes = 1;
    limits.retained_events = 16;
    limits.retained_clusters = 4;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(graph, budget);
    std::string error;
    assert(guarded.recover(error));
    const auto before = graph.state_version();
    assert(!guarded.set_question_memory_state("q-transient",
        QuestionMemoryState::Dormant, before, error));
    assert(error == "RESOURCE_BUDGET_EXCEEDED");
    assert(graph.state_version() == before);
    assert(graph.question_memory_state("q-transient") == QuestionMemoryState::Active);
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::TransientBytes)] == 0);
  }
  std::remove(question_transient_path.c_str());

  // The remaining single-record Question mutations must reserve transient
  // staging before entering the journal-backed mutator.
  const std::string remaining_question_transient_path =
      "budgeted_question_remaining_transient_test.journal";
  std::remove(remaining_question_transient_path.c_str());
  {
    EventJournal journal(remaining_question_transient_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    SemanticDelta create;
    create.delta_id = "question-remaining-transient-create";
    create.idempotency_key = "question-remaining-transient-create-key";
    create.expected_state_version = graph.state_version();
    create.nodes.push_back({{"q-reference", NodeKind::Question, "question", {}}});
    create.nodes.push_back({{"aspect-reference", NodeKind::QuestionAspect,
        "question aspect", {"q-reference"}}});
    assert(graph.apply(create).committed);

    ResourceBudgetLimits limits;
    limits.resident_bytes = 8192;
    limits.transient_bytes = 1;
    limits.retained_events = 32;
    limits.retained_clusters = 4;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(graph, budget);
    std::string error;
    assert(guarded.recover(error));

    const auto before = graph.state_version();
    bool reactivated = false;
    assert(!guarded.record_question_reference("q-reference", before,
        reactivated, error));
    assert(error == "RESOURCE_BUDGET_EXCEEDED");
    assert(graph.state_version() == before);
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::TransientBytes)] == 0);

    error.clear();
    assert(!guarded.set_question_aspect_status("aspect-reference",
        QuestionAspectStatus::Resolved, before, error));
    assert(error == "RESOURCE_BUDGET_EXCEEDED");
    assert(graph.state_version() == before);
    assert(graph.question_aspect_status("aspect-reference") == QuestionAspectStatus::Open);
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::TransientBytes)] == 0);
  }
  std::remove(remaining_question_transient_path.c_str());
  const std::string cluster_lifecycle_retention_path = "budgeted_cluster_lifecycle_retention_test.journal";
  std::remove(cluster_lifecycle_retention_path.c_str());
  {
    EventJournal journal(cluster_lifecycle_retention_path);
    assert(journal.open());
    ContextGraph graph(journal);
    assert(graph.open());
    SemanticDelta create;
    create.delta_id = "retention-seed";
    create.idempotency_key = "retention-seed-key";
    create.expected_state_version = graph.state_version();
    create.nodes = {{{"ret-root", NodeKind::Cluster, "root", {}}},
                    {{"ret-low", NodeKind::Cluster, "low priority", {}}}};
    create.relations.push_back({{"ret-low-parent", RelationKind::Contains,
        "ret-root", "ret-low"}});
    assert(graph.apply(create).committed);

    ResourceBudgetLimits limits;
    limits.resident_bytes = 65536;
    limits.transient_bytes = 65536;
    limits.retained_events = 128;
    limits.retained_clusters = 8;
    ResourceBudget budget(limits);
    BudgetedContextGraph guarded(graph, budget, 65536);
    std::string error;
    assert(guarded.recover(error));

    const std::vector<ClusterRetentionCandidate> candidates{
        {"ret-root", 1.0, 1, false}, {"ret-low", 0.0, 1, false}};
    const auto first_plan = ClusterRetentionPlanner::plan(graph, candidates, 1);
    assert(first_plan.accepted && first_plan.forget_cluster_ids.size() == 1);
    assert(first_plan.planned_state_version == graph.state_version());
    const auto first = guarded.apply_retention_plan(first_plan,
        "2026-11-20T00:00:00Z", "capacity pressure", std::string(64, 'a'),
        "retention-pass-1", "retention-pass-1-key", error);
    assert(first.committed && graph.cluster_lifecycle("ret-low")->state ==
        ClusterLifecycleState::Dormant);

    const auto stale = guarded.apply_retention_plan(first_plan,
        "2026-11-20T00:00:00Z", "capacity pressure", std::string(64, 'a'),
        "retention-pass-stale", "retention-pass-stale-key", error);
    assert(!stale.committed && error == "CLUSTER_RETENTION_PLAN_STALE");

    const auto second_plan = ClusterRetentionPlanner::plan(graph, candidates, 1);
    assert(second_plan.accepted && second_plan.forget_cluster_ids.size() == 1);
    const auto second = guarded.apply_retention_plan(second_plan,
        "2026-11-20T00:01:00Z", "continued capacity pressure", std::string(64, 'b'),
        "retention-pass-2", "retention-pass-2-key", error);
    assert(second.committed && graph.cluster_lifecycle("ret-low")->state ==
        ClusterLifecycleState::Archived);
  }
  std::remove(cluster_lifecycle_retention_path.c_str());

  return 0;
}
