#include "surge/context_graph.hpp"
#include "surge/context_hint.hpp"
#include "surge/dependency_engine.hpp"
#include "surge/packet_admission.hpp"
#include "surge/question_router.hpp"
#include "surge/sha256.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace surge;

int main() {
  const auto root = std::filesystem::temp_directory_path() / "surge-runtime-pipeline";
  std::filesystem::remove_all(root);
  const auto journal_path = (root / "runtime.sgj").string();
  const std::string source = "予算を増額する";
  const auto digest = sha256_hex(source);
  {
    EventJournal journal(journal_path);
    assert(journal.open());
    PacketAdmissionCoordinator packets(journal);
    assert(packets.recover());
    ContextGraph graph(journal);
    assert(graph.open());

    ValidatedRecognitionPacket packet;
    packet.packet_id = "runtime-packet-1";
    packet.idempotency_key = "runtime-idem-1";
    packet.context_id = "meeting-1";
    packet.source_id = "transcript-1";
    packet.source_generation = "session-1";
    packet.block_ref = "utterance-1";
    packet.offset_basis = "UTF8_BYTE_NFC";
    packet.start_offset = 0;
    packet.end_offset = source.size();
    packet.source_sequence = 1;
    packet.source_snapshot_digest = digest;
    packet.source_range_digest = digest;
    packet.canonical_request_digest = "canonical-runtime-packet";
    const auto admitted = packets.admit(packet, source);
    assert(admitted.admitted && admitted.state_version == 1);
    assert(packets.packet_count() == 1);

    SemanticDelta seed;
    seed.delta_id = "runtime-seed";
    seed.idempotency_key = "runtime-seed-idem";
    seed.expected_state_version = journal.state_version();
    seed.nodes = {
      {{"cluster-budget", NodeKind::Cluster, "予算案", {}}},
      {{"cluster-finance", NodeKind::Cluster, "財源", {}}},
      {{"evidence-a", NodeKind::Evidence, "増額を求める発言", {}}},
      {{"evidence-b", NodeKind::Evidence, "財源不足の指摘", {}}},
      {{"claim-a", NodeKind::Claim, "予算を増額する", {"evidence-a"}}},
      {{"claim-b", NodeKind::Claim, "予算は増額できない", {"evidence-b"}}},
      {{"dependency-budget-finance", NodeKind::Dependency,
        "予算判断は財源確認に依存", {"cluster-budget", "cluster-finance"}}}
    };
    seed.relations = {
      {{"about-a", RelationKind::About, "claim-a", "cluster-budget"}},
      {{"about-b", RelationKind::About, "claim-b", "cluster-finance"}},
      {{"contradiction-1", RelationKind::Contradicts, "claim-a", "claim-b"}}
    };
    const auto seeded = graph.apply(seed);
    assert(seeded.committed && seeded.state_version == 2);

    const auto routed = QuestionRouter::route(graph, "contradiction-1", "policy-v1");
    assert(routed.committed && !routed.focus_selected);
    assert(routed.interest_ids.size() == 2);
    assert(graph.nodes().count(routed.question_id) == 1);
    assert(graph.nodes().count(routed.question_aspect_id) == 1);
    assert(!graph.focus());

    const auto impact = DependencyEngine::compute(
        graph.nodes(), {"cluster-budget"}, 32);
    assert(impact.complete);
    assert(std::find(impact.impacted_nodes.begin(), impact.impacted_nodes.end(),
                     "cluster-finance") != impact.impacted_nodes.end());

    std::vector<HintCandidate> candidates = {
      {routed.question_aspect_id, HintClass::RequiredQuestionAspect, 0, 0},
      {"dependency-budget-finance", HintClass::TriggeredDependency, 1, 1},
      {"cluster-finance", HintClass::RelatedCluster, 1, 0},
      {"runtime-packet-1", HintClass::RequiredLineage, 0, 0}
    };
    const auto hint = ContextHintBuilder::build(
        "meeting-1", "runtime-packet-1", digest, "policy-v1",
        graph.state_version(), candidates, 1);
    assert(hint.error.empty() && hint.ticket_required);
    assert(!hint.hint.context_complete);
    RetrievalTicketRegistry tickets;
    assert(tickets.issue(hint.ticket));
    const auto resolved = tickets.resolve(hint.ticket.ticket_id,
        graph.state_version(), false);
    assert(resolved.status == TicketStatus::Resolved);
    assert(resolved.refs == hint.ticket.omitted_refs);
    assert(journal.state_version() == 3);
  }
  {
    EventJournal recovered_journal(journal_path);
    assert(recovered_journal.open());
    PacketAdmissionCoordinator packets(recovered_journal);
    assert(packets.recover());
    ContextGraph graph(recovered_journal);
    assert(graph.open());
    assert(packets.packet_count() == 1);
    assert(graph.state_version() == 3);
    assert(graph.nodes().count("cluster-budget") == 1);
    assert(graph.nodes().count("cluster-finance") == 1);
    assert(graph.nodes().count("question-aspect:contradiction-1") == 1 ||
           graph.nodes().size() >= 9);
  }
  std::filesystem::remove_all(root);
  std::cout << "SURGE end-to-end runtime pipeline passed: packet -> graph -> question/interest -> dependency -> context hint -> replay\n";
}
