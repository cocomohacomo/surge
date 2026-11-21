#include "surge/cluster_archive_store.hpp"
#include "surge/sha256.hpp"
#include "surge/budgeted_context_graph.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
namespace {
std::string make_payload(const std::string& cluster, std::uint64_t version,
                         const std::string& body = "nodes 0\nrelations 0\nhas_focus 0\nfocus_history 0\n") {
  std::ostringstream out;
  out << "SURGE_CLUSTER_ARCHIVE_PAYLOAD 2\nwatermark " << version
      << "\ncluster " << std::quoted(cluster) << "\n" << body
      << "scope_spans 0\n"
      << "claim_validity_histories 0\n"
      << "evidence_validity_histories 0\n"
      << "dependency_evaluations 0\n"
      << "reassessment_histories 0\n"
      << "question_states 0\n"
      << "question_aspect_states 0\n"
      << "claim_lifecycle_states 0\n";
  return out.str();
}
}
#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)
int main() {
  using namespace surge;
  const auto path = std::filesystem::temp_directory_path() / "surge_cluster_archive_test.bin";
  std::error_code ec;
  const std::string graph_path = "cluster_archive_detach_plan_test.journal";
  std::remove(graph_path.c_str());
  {
    EventJournal journal(graph_path);
    CHECK(journal.open());
    ContextGraph graph(journal);
    CHECK(graph.open());
    SemanticDelta seed;
    seed.delta_id = "detach-plan-seed";
    seed.idempotency_key = "detach-plan-seed-key";
    seed.expected_state_version = graph.state_version();
    seed.nodes.push_back({{"archive-a", NodeKind::Cluster, "archive target", {}}});
    seed.nodes.push_back({{"archive-b", NodeKind::Cluster, "other owner", {}}});
    seed.nodes.push_back({{"exclusive", NodeKind::Entity, "exclusive payload", {}}});
    seed.nodes.push_back({{"shared", NodeKind::Entity, "shared payload", {}}});
    seed.nodes.push_back({{"question", NodeKind::Question, "not detachable entity", {}}});
    seed.nodes.push_back({{"reference-leaf", NodeKind::Evidence, "leaf", {}}});
    seed.nodes.push_back({{"reference-middle", NodeKind::Claim, "middle", {"reference-leaf"}}});
    seed.relations.push_back({{"a-exclusive", RelationKind::Contains, "archive-a", "exclusive"}});
    seed.relations.push_back({{"a-shared", RelationKind::Contains, "archive-a", "shared"}});
    seed.relations.push_back({{"b-shared", RelationKind::Contains, "archive-b", "shared"}});
    seed.relations.push_back({{"a-question", RelationKind::Contains, "archive-a", "question"}});
    seed.relations.push_back({{"a-reference-middle", RelationKind::Contains, "archive-a", "reference-middle"}});
    CHECK(graph.apply(seed).committed);
    const auto plan = plan_cluster_archive_detach(graph, "archive-a",
        {"exclusive", "shared", "question", "missing", "exclusive"});
    CHECK(plan.exclusively_owned_entity_ids.size() == 1);
    CHECK(plan.exclusively_owned_entity_ids.front() == "exclusive");
    CHECK(plan.shared_member_ids.size() == 1);
    CHECK(plan.shared_member_ids.front() == "shared");
    CHECK(plan.ineligible_member_ids.size() == 2);
    CHECK(graph.nodes().count("exclusive") == 1);
    CHECK(graph.nodes().count("shared") == 1);
    auto archive_budget_limits = ResourceBudgetLimits::temporary_edge_200mb();
    archive_budget_limits.retained_events = 4096;
    archive_budget_limits.retained_clusters = 256;
    ResourceBudget budget(archive_budget_limits);
    BudgetedContextGraph budgeted(graph, budget);
    std::string detach_error;
    if (!budgeted.recover(detach_error)) {
      std::cerr << "BudgetedContextGraph recovery failed: " << detach_error << "\\n";
      return 1;
    }
    const auto detach_path = std::filesystem::temp_directory_path() /
        "surge_cluster_archive_detach_test.bin";
    std::filesystem::remove(detach_path, ec);
    std::filesystem::remove(detach_path.string()+".bak", ec);
    ClusterArchiveStore detach_store(detach_path.u8string());
    std::string canonical_payload;
    CHECK(serialize_cluster_archive_payload(graph, "archive-a", &canonical_payload, &detach_error));
    CHECK(canonical_payload.find("SURGE_CLUSTER_ARCHIVE_PAYLOAD 2") == 0);
    CHECK(canonical_payload.find("scope_spans ") != std::string::npos);
    CHECK(canonical_payload.find("claim_validity_histories ") != std::string::npos);
    CHECK(canonical_payload.find("evidence_validity_histories ") != std::string::npos);
    CHECK(canonical_payload.find("dependency_evaluations ") != std::string::npos);
    CHECK(canonical_payload.find("reassessment_histories ") != std::string::npos);
    CHECK(canonical_payload.find("question_states ") != std::string::npos);
    CHECK(canonical_payload.find("question_aspect_states ") != std::string::npos);
    CHECK(canonical_payload.find("claim_lifecycle_states ") != std::string::npos);
    CHECK(canonical_payload.find("\"reference-middle\"") != std::string::npos);
    CHECK(canonical_payload.find("\"reference-leaf\"") != std::string::npos);
    CHECK(validate_cluster_archive_payload_identity(canonical_payload, "archive-a",
        graph.state_version(), &detach_error));
    CHECK(validate_cluster_archive_payload_graph_records(canonical_payload, &detach_error));
    {
      const std::string restore_journal_path = "cluster_archive_restore_test.journal";
      std::remove(restore_journal_path.c_str());
      EventJournal restore_journal(restore_journal_path);
      CHECK(restore_journal.open());
      ContextGraph restored_graph(restore_journal);
      CHECK(restored_graph.open());
      auto restore_limits = ResourceBudgetLimits::temporary_edge_200mb();
      restore_limits.retained_events = 4096;
      restore_limits.retained_clusters = 256;
      ResourceBudget restore_budget(restore_limits);
      BudgetedContextGraph restore_facade(restored_graph, restore_budget);
      std::string restore_error;
      CHECK(restore_facade.recover(restore_error));
      ClusterArchiveRecord restore_record;
      restore_record.cluster_id = "archive-a";
      restore_record.state_version = graph.state_version();
      restore_record.journal_sequence = graph.state_version();
      restore_record.payload = canonical_payload;
      const auto restored = restore_cluster_archive_graph_records(
          restore_record, restored_graph, restore_facade, restored_graph.state_version(),
          "restore-archive-a", "restore-archive-a-key");
      CHECK(restored.committed && restored.added_nodes > 0);
      CHECK(restored_graph.nodes().count("archive-a") == 1);
      CHECK(restored_graph.nodes().count("reference-middle") == 1);
      CHECK(restored_graph.nodes().count("reference-leaf") == 1);
      CHECK(restored_graph.relations().count("a-exclusive") == 1);
      const auto repeated = restore_cluster_archive_graph_records(
          restore_record, restored_graph, restore_facade, restored_graph.state_version(),
          "restore-archive-a-repeat", "restore-archive-a-repeat-key");
      CHECK(repeated.committed && repeated.added_nodes == 0 &&
            repeated.added_relations == 0);

      const auto stable_version = restored_graph.state_version();
      auto conflicting_record = restore_record;
      const auto content_pos = conflicting_record.payload.find("archive target");
      CHECK(content_pos != std::string::npos);
      conflicting_record.payload.replace(content_pos,
          std::string("archive target").size(), "conflicting target");
      const auto conflict = restore_cluster_archive_graph_records(
          conflicting_record, restored_graph, restore_facade, stable_version,
          "restore-conflict", "restore-conflict-key");
      CHECK(!conflict.committed &&
            conflict.error == "CLUSTER_ARCHIVE_GRAPH_RESTORE_NODE_CONFLICT");
      CHECK(restored_graph.state_version() == stable_version);
      CHECK(restored_graph.nodes().at("archive-a").content == "archive target");

      const auto stale = restore_cluster_archive_graph_records(
          restore_record, restored_graph, restore_facade, stable_version - 1,
          "restore-stale", "restore-stale-key");
      CHECK(!stale.committed &&
            stale.error == "CLUSTER_ARCHIVE_GRAPH_RESTORE_STALE_TARGET");
      CHECK(restored_graph.state_version() == stable_version);

      const std::string focus_payload = make_payload("focus-cluster", 2,
          R"(nodes 3
node "focus-cluster" 0 "focus target" 0
node "focus-aspect" 6 "focus aspect" 0
node "focus-interest" 8 "interest" 2 "focus-aspect" "focus-cluster"
relations 0
has_focus 1
focus "focus-cluster" "focus-interest" 0
focus_history 0
)");
      CHECK(validate_cluster_archive_payload_graph_records(focus_payload, &restore_error));
      ClusterArchiveRecord focus_record;
      focus_record.cluster_id = "focus-cluster";
      focus_record.state_version = 2;
      focus_record.journal_sequence = 2;
      focus_record.payload = focus_payload;
      const auto focus_restored = restore_cluster_archive_graph_records(
          focus_record, restored_graph, restore_facade, restored_graph.state_version(),
          "restore-focus", "restore-focus-key");
      if (!focus_restored.committed) {
        std::cerr << "Focus archive restore failed: " << focus_restored.error << "\\n";
      }
      CHECK(focus_restored.committed);
      CHECK(restored_graph.focus().has_value());
      CHECK(restored_graph.focus()->cluster_id == "focus-cluster");
      CHECK(restored_graph.focus()->interest_id == "focus-interest");
      const auto focus_repeat = restore_cluster_archive_graph_records(
          focus_record, restored_graph, restore_facade, restored_graph.state_version(),
          "restore-focus-repeat", "restore-focus-repeat-key");
      CHECK(focus_repeat.committed && focus_repeat.duplicate);

      std::remove(restore_journal_path.c_str());
    }

    const std::string duplicate_nodes = make_payload("cluster-a", graph.state_version(),
        "nodes 2\nnode \"n1\" 2 \"a\" 0\nnode \"n1\" 2 \"b\" 0\nrelations 0\nhas_focus 0\nfocus_history 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(duplicate_nodes, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_GRAPH_RECORDS_INVALID");
    const std::string invalid_enum = make_payload("cluster-a", graph.state_version(),
        "nodes 1\nnode \"n1\" 99 \"a\" 0\nrelations 0\nhas_focus 0\nfocus_history 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(invalid_enum, &detach_error));
    const std::string dangling_relation = make_payload("cluster-a", graph.state_version(),
        "nodes 1\nnode \"n1\" 2 \"a\" 0\nrelations 1\nrelation \"r1\" 9 \"n1\" \"missing\"\nhas_focus 0\nfocus_history 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(dangling_relation, &detach_error));
    const std::string dangling_reference = make_payload("cluster-a", graph.state_version(),
        "nodes 1\nnode \"n1\" 2 \"a\" 1 \"missing\"\nrelations 0\nhas_focus 0\nfocus_history 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(dangling_reference, &detach_error));
    const std::string focus_non_cluster = make_payload("cluster-a", graph.state_version(),
        "nodes 1\nnode \"n1\" 2 \"entity\" 0\nrelations 0\nhas_focus 1\nfocus \"n1\" \"\" 0\nfocus_history 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(focus_non_cluster, &detach_error));
    const std::string focus_non_interest = make_payload("cluster-a", graph.state_version(),
        "nodes 2\nnode \"c1\" 0 \"cluster\" 0\nnode \"e1\" 2 \"entity\" 0\nrelations 0\nhas_focus 1\nfocus \"c1\" \"e1\" 0\nfocus_history 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(focus_non_interest, &detach_error));
    const std::string focus_chaos_mismatch = make_payload("cluster-a", graph.state_version(),
        "nodes 1\nnode \"c1\" 0 \"cluster\" 0\nrelations 0\nhas_focus 1\nfocus \"c1\" \"\" 1\nfocus_history 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(focus_chaos_mismatch, &detach_error));
    const std::string valid_histories =
        "SURGE_CLUSTER_ARCHIVE_PAYLOAD 2\nwatermark " + std::to_string(graph.state_version() + 1) +
        "\ncluster \"cluster-a\"\nnodes 2\n"
        "node \"c1\" 4 \"claim\" 1 \"e1\"\n"
        "node \"e1\" 3 \"evidence\" 0\n"
        "relations 0\nhas_focus 0\nfocus_history 0\nscope_spans 0\n"
        "claim_validity_histories 1\nclaim_history \"c1\" 1\n"
        "claim_validity \"c1\" \"2026-01-01T00:00:00Z\" \"\" \"e1\" 1 \"\" \"\" 1\n"
        "evidence_validity_histories 1\nevidence_history \"e1\" 1\n"
        "evidence_validity \"e1\" \"2026-01-01T00:00:00Z\" \"\" \"e1\" 1 \"\" \"\" 2\n"
        "dependency_evaluations 0\n"
        "reassessment_histories 0\nquestion_states 0\n"
        "question_aspect_states 0\nclaim_lifecycle_states 1\n"
        "claim_status \"c1\" 0\n";
    CHECK(validate_cluster_archive_payload_graph_records(valid_histories, &detach_error));
    const std::string valid_dependency =
        "SURGE_CLUSTER_ARCHIVE_PAYLOAD 2\nwatermark " + std::to_string(graph.state_version()) +
        "\ncluster \"cluster-a\"\nnodes 3\n"
        "node \"dep1\" 7 \"dependency\" 2 \"q1\" \"q2\"\n"
        "node \"q1\" 5 \"prerequisite\" 0\n"
        "node \"q2\" 5 \"dependent\" 0\n"
        "relations 0\nhas_focus 0\nfocus_history 0\nscope_spans 0\n"
        "claim_validity_histories 0\nevidence_validity_histories 0\n"
        "dependency_evaluations 1\ndependency \"delta-1\" \"delta-1\" 1\n"
        "trigger \"dep1\" \"q1\" \"q2\"\n"
        "impacted 2 \"q1\" \"q2\"\nrejected 0\n"
        "reassessment_histories 0\nquestion_states 2\n"
        "question_state \"q1\" 0 0\nquestion_state \"q2\" 0 0\n"
        "question_aspect_states 0\nclaim_lifecycle_states 0\n";
    CHECK(validate_cluster_archive_payload_graph_records(valid_dependency, &detach_error));
    std::string bad_dependency_endpoint = valid_dependency;
    const auto bad_endpoint_pos = bad_dependency_endpoint.find("trigger ");
    CHECK(bad_endpoint_pos != std::string::npos);
    const auto bad_endpoint_end = bad_dependency_endpoint.find('\n', bad_endpoint_pos);
    bad_dependency_endpoint.replace(bad_endpoint_pos, bad_endpoint_end - bad_endpoint_pos,
                                    "trigger \"dep1\" \"missing\" \"q2\"");
    CHECK(!validate_cluster_archive_payload_graph_records(bad_dependency_endpoint, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_TRIGGER_INVALID");
    std::string bad_dependency_rejection = valid_dependency;
    const auto rejection_pos = bad_dependency_rejection.find("rejected 0");
    CHECK(rejection_pos != std::string::npos);
    bad_dependency_rejection.replace(rejection_pos, std::string("rejected 0").size(),
                                    "rejected 1 \"q1\"");
    CHECK(!validate_cluster_archive_payload_graph_records(bad_dependency_rejection, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_DEPENDENCY_REJECTION_INVALID");
    const std::string reassessment_snapshot =
        "\"trace1\" \"c1\" \"2026-01-01T00:00:00Z\" \"SUPPORTED\" "
        "\"2026-01-01T00:00:00Z\" \"2026-12-31T00:00:00Z\" \"e1\" "
        "\"ACTIVE_SUPPORT\" 1 1 1 2 1 0 2 1 \"e1\" \"rel1\" 0 1 1 "
        "\"2026-01-01T00:00:00Z\" \"2026-12-31T00:00:00Z\" \"e1\" 2 1 "
        "1 \"e1\" 0";
    const std::string previous_hash(64, '0');
    const std::string snapshot_digest = sha256_hex(reassessment_snapshot);
    const std::string chain_hash = sha256_hex(previous_hash + snapshot_digest);
    const std::string valid_reassessment =
        "SURGE_CLUSTER_ARCHIVE_PAYLOAD 2\nwatermark " + std::to_string(graph.state_version() + 1) +
        "\ncluster \"cluster-a\"\nnodes 2\n"
        "node \"c1\" 4 \"claim\" 0\nnode \"e1\" 3 \"evidence\" 0\n"
        "relations 1\nrelation \"rel1\" 0 \"e1\" \"c1\"\n"
        "has_focus 0\nfocus_history 0\nscope_spans 0\n"
        "claim_validity_histories 0\nevidence_validity_histories 0\n"
        "dependency_evaluations 0\nreassessment_histories 1\n"
        "reassessment \"c1\" 1\n"
        "reassessment_record \"trace1\" \"c1\" \"2026-01-01T00:00:00Z\" "
        "\"SUPPORTED\" \"2026-01-01T00:00:00Z\" \"2026-12-31T00:00:00Z\" "
        "\"e1\" \"ACTIVE_SUPPORT\" 1 1 1 2 1 0 2 "
        "\"" + previous_hash + "\" \"" + snapshot_digest + "\" \"" + chain_hash + "\" 1\n"
        "reassessment_evidence \"e1\" \"rel1\" 0 1 1 "
        "\"2026-01-01T00:00:00Z\" \"2026-12-31T00:00:00Z\" \"e1\" 2 1\n"
        "active_supports 1 \"e1\"\nactive_contradictions 0\n"
        "question_states 0\nquestion_aspect_states 0\n"
        "claim_lifecycle_states 1\nclaim_status \"c1\" 0\n";
    CHECK(validate_cluster_archive_payload_graph_records(valid_reassessment, &detach_error));
    std::string tampered_reassessment = valid_reassessment;
    const auto chain_pos = tampered_reassessment.find(chain_hash);
    CHECK(chain_pos != std::string::npos);
    tampered_reassessment.replace(chain_pos, chain_hash.size(), std::string(64, 'f'));
    CHECK(!validate_cluster_archive_payload_graph_records(tampered_reassessment, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_REASSESSMENT_CHAIN_INVALID");
    const std::string trailing_data = valid_dependency + "unexpected tail\\n";
    CHECK(!validate_cluster_archive_payload_graph_records(trailing_data, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_TRAILING_DATA_INVALID");
    std::string missing_question_state = valid_dependency;
    const auto question_state_pos = missing_question_state.find("question_states 2");
    CHECK(question_state_pos != std::string::npos);
    missing_question_state.replace(question_state_pos,
        std::string("question_states 2").size(), "question_states 1");
    CHECK(!validate_cluster_archive_payload_graph_records(missing_question_state, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_QUESTION_STATE_INVALID");
    const std::string bad_claim_history = make_payload("cluster-a", graph.state_version(),
        "nodes 2\nnode \"c1\" 4 \"claim\" 1 \"e1\"\nnode \"e1\" 3 \"evidence\" 0\nrelations 0\nhas_focus 0\nfocus_history 0\nscope_spans 0\nclaim_validity_histories 1\nclaim_history \"c1\" 1\nclaim_validity \"c1\" \"2026-01-01T00:00:00Z\" \"\" \"e1\" 2 \"\" \"\" 1\nevidence_validity_histories 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(bad_claim_history, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_CLAIM_VALIDITY_INVALID");
    const std::string bad_claim_timestamp = make_payload("cluster-a", graph.state_version(),
        "nodes 2\nnode \"c1\" 4 \"claim\" 1 \"e1\"\nnode \"e1\" 3 \"evidence\" 0\nrelations 0\nhas_focus 0\nfocus_history 0\nscope_spans 0\nclaim_validity_histories 1\nclaim_history \"c1\" 1\nclaim_validity \"c1\" \"not-a-timestamp\" \"\" \"e1\" 1 \"\" \"\" 1\nevidence_validity_histories 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(bad_claim_timestamp, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_CLAIM_VALIDITY_INVALID");
    const std::string unbound_evidence_basis = make_payload("cluster-a", graph.state_version(),
        "nodes 3\nnode \"c1\" 4 \"claim\" 1 \"e1\"\nnode \"e1\" 3 \"evidence\" 0\nnode \"e2\" 3 \"evidence\" 0\nrelations 0\nhas_focus 0\nfocus_history 0\nscope_spans 0\nclaim_validity_histories 1\nclaim_history \"c1\" 1\nclaim_validity \"c1\" \"2026-01-01T00:00:00Z\" \"\" \"e2\" 1 \"\" \"\" 1\nevidence_validity_histories 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(unbound_evidence_basis, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_CLAIM_VALIDITY_INVALID");
    const std::string bad_evidence_owner = make_payload("cluster-a", graph.state_version(),
        "nodes 1\nnode \"e1\" 3 \"evidence\" 0\nrelations 0\nhas_focus 0\nfocus_history 0\nscope_spans 0\nclaim_validity_histories 0\nevidence_validity_histories 1\nevidence_history \"missing\" 1\nevidence_validity \"missing\" \"2026-01-01T00:00:00Z\" \"\" \"e1\" 1 \"\" \"\" 1\n");
    CHECK(!validate_cluster_archive_payload_graph_records(bad_evidence_owner, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_EVIDENCE_VALIDITY_INVALID");
    const std::string scope_bad_anchor = make_payload("cluster-a", graph.state_version(),
        "nodes 0\nrelations 0\nhas_focus 0\nfocus_history 0\nscope_spans 1\nscope \"s1\" 0 \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" \"UTF8_BYTE_NFC\" \"a1\" \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" \"UTF8_BYTE_NFC\" 0 0 \"x\" \"packet-1\" 0\n0\nscope_antecedents 0\nscope_meta \"\" \"\" \"\" \"\" 1 \"packet-1\"\nscope_result 0 0 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(scope_bad_anchor, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    const std::string scope_bad_digest = make_payload("cluster-a", graph.state_version(),
        "nodes 0\nrelations 0\nhas_focus 0\nfocus_history 0\nscope_spans 1\nscope \"s1\" 0 \"bad-digest\" \"UTF8_BYTE_NFC\" \"a1\" \"bad-digest\" \"UTF8_BYTE_NFC\" 1 2 \"x\" \"packet-1\" 0\n0\nscope_antecedents 0\nscope_meta \"\" \"\" \"\" \"\" 1 \"packet-1\"\nscope_result 0 0 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(scope_bad_digest, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_SCOPE_SPAN_INVALID");
    const std::string count_mismatch = make_payload("cluster-a", graph.state_version(),
        "nodes 1\nrelations 0\nhas_focus 0\nfocus_history 0\n");
    CHECK(!validate_cluster_archive_payload_graph_records(count_mismatch, &detach_error));
    CHECK(!validate_cluster_archive_payload_identity(canonical_payload, "archive-b",
        graph.state_version(), &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_CLUSTER_MISMATCH");
    CHECK(!validate_cluster_archive_payload_identity(canonical_payload, "archive-a",
        graph.state_version() + 1, &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_WATERMARK_MISMATCH");
    CHECK(!validate_cluster_archive_payload_identity("legacy payload", "archive-a",
        graph.state_version(), &detach_error));
    CHECK(detach_error == "CLUSTER_ARCHIVE_PAYLOAD_FORMAT_UNSUPPORTED");
    const auto before_invalid_detach = graph.state_version();
    const auto invalid_detach = archive_and_detach_cluster(graph, budgeted, detach_store,
        "archive-a", {"exclusive"},
        "SURGE_CLUSTER_ARCHIVE_PAYLOAD 2\nwatermark 999\ncluster \"archive-a\"\n",
        before_invalid_detach, "invalid-archive-detach", "invalid-archive-detach-key");
    CHECK(!invalid_detach.archive_verified && !invalid_detach.graph_detached);
    CHECK(invalid_detach.error == "CLUSTER_ARCHIVE_PAYLOAD_WATERMARK_MISMATCH");
    CHECK(graph.state_version() == before_invalid_detach);
    CHECK(graph.nodes().count("exclusive") == 1);
    CHECK(canonical_payload.find("\\n") == std::string::npos);
    CHECK(!canonical_payload.empty() && canonical_payload.back() == '\n');
    std::string canonical_payload_again;
    CHECK(serialize_cluster_archive_payload(graph, "archive-a", &canonical_payload_again, &detach_error));
    CHECK(canonical_payload == canonical_payload_again);
    const auto before_detach = graph.state_version();
    const auto detached = archive_and_detach_cluster(graph, budgeted, detach_store,
        "archive-a", {"exclusive", "shared", "question", "missing", "exclusive"}, before_detach,
        "archive-detach-commit", "archive-detach-key");
    CHECK(detached.archive_verified);
    CHECK(detached.graph_detached);
    CHECK(detached.reclaimed_entity_ids.size() == 1);
    CHECK(detached.reclaimed_entity_ids.front() == "exclusive");
    CHECK(detached.preserved_shared_member_ids.size() == 1);
    CHECK(detached.preserved_shared_member_ids.front() == "shared");
    CHECK(graph.nodes().count("exclusive") == 0);
    CHECK(graph.nodes().count("shared") == 1);
    CHECK(graph.nodes().count("question") == 1);
    CHECK(graph.cluster_membership_reference_count("shared") == 1);
    std::filesystem::remove(detach_path, ec);
    std::filesystem::remove(detach_path.string()+".bak", ec);
    std::filesystem::remove(detach_path.string()+".tmp", ec);
  }
  std::remove(graph_path.c_str());
  std::filesystem::remove(path, ec); std::filesystem::remove(path.string()+".bak", ec);
  std::filesystem::remove(path.string()+".tmp", ec);
  ClusterArchiveStore store(path.u8string());
  std::string error;
  CHECK(!store.archive("", 1, 1, "payload", &error));
  CHECK(error == "CLUSTER_ARCHIVE_INPUT_INVALID");
  CHECK(!store.archive("cluster-a", 0, 1, "payload", &error));
  CHECK(!store.archive("cluster-a", 7, 12, "legacy payload", &error));
  CHECK(error == "CLUSTER_ARCHIVE_PAYLOAD_FORMAT_UNSUPPORTED");
  CHECK(!store.archive("cluster-a", 7, 12,
      "SURGE_CLUSTER_ARCHIVE_PAYLOAD 2\nwatermark 7\ncluster \"cluster-a\"\n", &error));
  CHECK(error == "CLUSTER_ARCHIVE_PAYLOAD_NODE_SECTION_INVALID");
  CHECK(!store.archive("cluster-a", 7, 12,
      make_payload("cluster-a", 7, "nodes invalid\n"), &error));
  CHECK(error == "CLUSTER_ARCHIVE_PAYLOAD_NODE_SECTION_INVALID");
  CHECK(!store.archive("cluster-a", 7, 12, make_payload("other-cluster", 7), &error));
  CHECK(error == "CLUSTER_ARCHIVE_PAYLOAD_CLUSTER_MISMATCH");
  CHECK(!store.archive("cluster-a", 7, 12, make_payload("cluster-a", 8), &error));
  CHECK(error == "CLUSTER_ARCHIVE_PAYLOAD_WATERMARK_MISMATCH");
  const std::string large_payload = make_payload("cluster-a", 7, "nodes 1\nnode \"n1\" 2 \"large-ish payload\" 0\nrelations 0\nhas_focus 0\nfocus_history 0\n");
  CHECK(store.archive("cluster-a", 7, 12, large_payload, &error));
  ClusterArchiveRecord record;
  CHECK(store.archive_verified("cluster-a", 7, 12, large_payload, &record, &error));
  CHECK(record.cluster_id == "cluster-a" && record.state_version == 7);
  CHECK(record.journal_sequence == 12 && record.payload == large_payload);
  std::string expanded_content(3U * 1024U * 1024U + 37U, 'x');
  std::ostringstream expanded_body;
  expanded_body << "nodes 1\nnode \"n1\" 2 " << std::quoted(expanded_content)
                << " 0\nrelations 0\nhas_focus 0\nfocus_history 0\n";
  const std::string expanded_payload = make_payload("cluster-a", 7, expanded_body.str());
  CHECK(store.archive_verified("cluster-a", 7, 13, expanded_payload, &record, &error));
  CHECK(record.payload == expanded_payload);
  CHECK(record.state_version == 7 && record.journal_sequence == 13);
  CHECK(!store.archive_verified("cluster-a", 7, 13, "payload", nullptr, &error));
  CHECK(error == "CLUSTER_ARCHIVE_VERIFY_OUTPUT_INVALID");
  CHECK(record.journal_sequence == 13 && record.payload == expanded_payload);
  ClusterArchiveRecord mismatched_record;
  const auto same_watermark_different_bytes = make_payload("cluster-a", 7,
      "nodes 1\nnode \"n1\" 2 \"different bytes\" 0\nrelations 0\nhas_focus 0\nfocus_history 0\n");
  CHECK(!store.archive_verified("cluster-a", 7, 13,
      same_watermark_different_bytes, &mismatched_record, &error));
  CHECK(error == "CLUSTER_ARCHIVE_VERIFY_MISMATCH");
  CHECK(!store.restore("cluster-a", 7, &record, &error, 14));
  CHECK(error == "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH");
  CHECK(store.restore("cluster-a", 7, &record, &error, 13));
  CHECK(!store.restore("cluster-b", 1, &record, &error));
  CHECK(error == "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH");
  CHECK(!store.restore("cluster-a", 8, &record, &error));
  CHECK(error == "CLUSTER_ARCHIVE_IDENTITY_OR_VERSION_MISMATCH");
  const std::string newer_payload = make_payload("cluster-a", 8);
  CHECK(store.archive("cluster-a", 8, 14, newer_payload, &error));
  CHECK(store.restore("cluster-a", 8, &record, &error));
  CHECK(record.state_version == 8 && record.payload == newer_payload);
  { std::ofstream out(path, std::ios::binary | std::ios::trunc);
    CHECK(static_cast<bool>(out)); out << "corrupt"; CHECK(static_cast<bool>(out)); }
  CHECK(store.restore("cluster-a", 7, &record, &error));
  CHECK(record.state_version == 7 && record.payload == expanded_payload);
  std::filesystem::remove(path, ec); std::filesystem::remove(path.string()+".bak", ec);
  std::filesystem::remove(path.string()+".tmp", ec);
  std::cout << "SURGE cluster archive store tests passed\n";
  return 0;
}
