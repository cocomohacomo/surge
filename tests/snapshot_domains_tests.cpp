#include "surge/snapshot_domains.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)

namespace {
void register_all(surge::SnapshotCoordinator& coordinator,
                  surge::ContextGraphSnapshotDomain& graph,
                  surge::EvidenceProvenanceSnapshotDomain& evidence,
                  surge::ChaosClusterSnapshotDomain& chaos,
                  surge::TemporalBoundarySnapshotDomain& boundary,
                  surge::TemporalQuestionSnapshotDomain& questions,
                  std::string& error) {
  CHECK(coordinator.register_domain(&graph, &error));
  CHECK(coordinator.register_domain(&evidence, &error));
  CHECK(coordinator.register_domain(&chaos, &error));
  CHECK(coordinator.register_domain(&boundary, &error));
  CHECK(coordinator.register_domain(&questions, &error));
}
}

int main() {
  using namespace surge;
  const auto dir = std::filesystem::temp_directory_path();
  const auto path = (dir / "surge_snapshot_tail_replay.journal").string();
  const auto copy_path = (dir / "surge_snapshot_tail_replay_full.journal").string();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(copy_path, ec);

  EventJournal journal(path);
  CHECK(journal.open());
  EvidenceProvenanceLedger evidence(journal);
  CHECK(evidence.replay());
  ChaosClusterLedger chaos(journal);
  CHECK(chaos.open());
  TemporalBoundaryLedger boundary(journal);
  CHECK(boundary.replay());
  TemporalQuestionLedger questions(journal);
  CHECK(questions.replay());
  ContextGraph graph(journal);
  CHECK(graph.open());

  std::string error;
  ChaosProvenance cp{"test", "1", "policy-1", "2026-11-19T00:00:00Z"};
  CHECK(chaos.create("tx-create-a", "idem-create-a", journal.state_version(),
                     "chaos-a", cp).committed);

  // Seed every nested Chaos history family before the checkpoint so the
  // snapshot path, not only journal replay, must preserve the full record.
  ChaosMember chaos_member_a{"chaos-member-a", "chaos-event-a", "NO_STABLE_TOPIC",
      "2026-11-19T00:00:01Z", "", cp};
  ChaosMember chaos_member_b{"chaos-member-b", "chaos-event-b", "CREATIVE_EXPLORATION",
      "2026-11-19T00:00:02Z", "", cp};
  CHECK(chaos.admit_member("chaos-member-a-add", "chaos-member-a-add",
      journal.state_version(), "chaos-a", chaos_member_a).committed);
  CHECK(chaos.admit_member("chaos-member-b-add", "chaos-member-b-add",
      journal.state_version(), "chaos-a", chaos_member_b).committed);
  CHECK(chaos.revise_membership("chaos-member-a-review", "chaos-member-a-review",
      journal.state_version(), "chaos-a", "chaos-member-a", "REVIEWED", true, cp).committed);
  ChaosAssociation chaos_association{"chaos-association-a", "chaos-member-a",
      "chaos-member-b", ChaosAssociationType::Analogy,
      ChaosAssociationStatus::Provisional, cp};
  chaos_association.valid_from = "2026-11-19T00:00:00Z";
  chaos_association.valid_until = "2026-11-20T00:00:00Z";
  CHECK(chaos.propose_association("chaos-association-add", "chaos-association-add",
      journal.state_version(), "chaos-a", chaos_association).committed);
  CHECK(chaos.review_association("chaos-association-review", "chaos-association-review",
      journal.state_version(), "chaos-a", "chaos-association-a",
      ChaosAssociationStatus::Rejected, cp).committed);
  ChaosPatternHypothesis chaos_hypothesis;
  chaos_hypothesis.hypothesis_id = "chaos-hypothesis-a";
  chaos_hypothesis.statement = "a provisional shared pattern";
  chaos_hypothesis.member_refs = {"chaos-member-a"};
  chaos_hypothesis.excluded_member_refs = {"chaos-member-b"};
  chaos_hypothesis.provenance = cp;
  CHECK(chaos.add_pattern_hypothesis("chaos-hypothesis-add", "chaos-hypothesis-add",
      journal.state_version(), "chaos-a", chaos_hypothesis).committed);
  CHECK(chaos.review_pattern_hypothesis("chaos-hypothesis-review",
      "chaos-hypothesis-review", journal.state_version(), "chaos-a",
      "chaos-hypothesis-a", "REVIEWABLE", "UNVERIFIED",
      {{"chaos-member-b", "insufficient independent support"}}, cp).committed);
  ChaosScoreObservation chaos_score{0.83, "snapshot-scorer", "1.0",
      "snapshot-policy", "2026-11-19T00:03:00Z"};
  CHECK(chaos.record_observation("chaos-score-record", "chaos-score-record",
      journal.state_version(), "chaos-a", "novelty", chaos_score).committed);
  CHECK(chaos.record_entity_observation("chaos-member-score", "chaos-member-score",
      journal.state_version(), "chaos-a", "MEMBER", "chaos-member-a", chaos_score).committed);
  CHECK(chaos.record_entity_observation("chaos-association-score", "chaos-association-score",
      journal.state_version(), "chaos-a", "ASSOCIATION", "chaos-association-a", chaos_score).committed);
  CHECK(chaos.record_entity_observation("chaos-hypothesis-score", "chaos-hypothesis-score",
      journal.state_version(), "chaos-a", "HYPOTHESIS", "chaos-hypothesis-a", chaos_score).committed);
  CHECK(chaos.transition("chaos-a-reviewable", "chaos-a-reviewable",
      journal.state_version(), "chaos-a", ChaosLifecycle::Reviewable,
      "checkpoint history fixture", cp).committed);

  EvidenceProvenance ep;
  ep.evidence_id = "evidence-a";
  ep.revision = 1;
  ep.observed_at = "2026-11-19T00:00:00Z";
  EvidenceProvenanceRecord er;
  CHECK(evidence.append(ep, journal.state_version(), er, error));

  auto b1 = boundary.add("btx1", "bidem1", "b-event-1", journal.state_version(),
      {"claim/A", "VALID_FROM", "2026-04-01T00:00:00+09:00", "",
       "evidence/b1", "packet/b1"});
  CHECK(b1.committed);
  auto b2 = boundary.add("btx2", "bidem2", "b-event-2", journal.state_version(),
      {"claim/A", "VALID_FROM", "2026-05-01T00:00:00+09:00", "",
       "evidence/b2", "packet/b2"});
  CHECK(b2.committed && b2.status == "BOUNDARY_CONFLICT_REVIEW_REQUIRED");
  auto q1 = questions.synchronize("qtx1", "qidem1", "q-event-1",
                                  journal.state_version(), b2.pending);
  CHECK(q1.committed && q1.status == "OPEN");

  SemanticDelta seed;
  seed.delta_id = "graph-seed";
  seed.idempotency_key = "graph-seed";
  seed.expected_state_version = journal.state_version();
  seed.nodes = {
      {{"cluster/G", NodeKind::Cluster, "approved", {}}},
      {{"evidence/G", NodeKind::Evidence, "source evidence", {}}},
      {{"claim/G", NodeKind::Claim, "claim text", {"evidence/G"}}},
      {{"question/G", NodeKind::Question, "unresolved question", {}}},
      {{"aspect/G", NodeKind::QuestionAspect, "validity aspect", {"question/G"}}},
      {{"interest/G", NodeKind::Interest, "interest", {"aspect/G", "cluster/G"}}}};
  seed.relations = {{{"supports/G", RelationKind::Supports, "evidence/G", "claim/G"}}};
  seed.focus = SetFocus{{"cluster/G", "interest/G", false}};
  seed.claim_validity_updates = {
      {"claim/G", "2026-01-01T00:00:00Z", "2027-01-01T00:00:00Z",
       "evidence/G", 1, "", ""}};
  seed.evidence_validity_updates = {
      {"evidence/G", "2026-01-01T00:00:00Z", "2027-01-01T00:00:00Z",
       "evidence/G", 1, "", ""}};
  CHECK(graph.apply(seed).committed);
  bool reactivated = false;
  CHECK(graph.set_question_aspect_status("aspect/G", QuestionAspectStatus::Resolved,
                                         graph.state_version(), error));
  CHECK(graph.record_question_reference("question/G", graph.state_version(),
                                        reactivated, error));
  CHECK(graph.set_question_memory_state("question/G", QuestionMemoryState::Dormant,
                                        graph.state_version(), error));

  ContextGraphSnapshotDomain gd(graph);
  EvidenceProvenanceSnapshotDomain ed(evidence);
  ChaosClusterSnapshotDomain cd(chaos);
  TemporalBoundarySnapshotDomain bd(boundary);
  TemporalQuestionSnapshotDomain qd(questions);
  SnapshotCoordinator coordinator;
  register_all(coordinator, gd, ed, cd, bd, qd, error);

  const auto checkpoint_version = journal.state_version();
  std::string snapshot;
  CHECK(coordinator.capture("ctx", checkpoint_version, checkpoint_version,
                            &snapshot, &error));

  // Append one transaction to every stateful domain after the checkpoint.
  ep.revision = 2;
  ep.observed_at = "2026-11-19T00:01:00Z";
  CHECK(evidence.append(ep, journal.state_version(), er, error));
  CHECK(chaos.create("tx-create-b", "idem-create-b", journal.state_version(),
                     "chaos-b", cp).committed);
  auto b3 = boundary.add("btx3", "bidem3", "b-event-3", journal.state_version(),
      {"claim/A", "VALID_FROM", "2026-06-01T00:00:00+09:00", "",
       "evidence/b3", "packet/b3"});
  CHECK(b3.committed);
  auto q2 = questions.synchronize("qtx2", "qidem2", "q-event-2",
                                  journal.state_version(), b3.pending);
  CHECK(q2.committed && q2.status == "UNDER_REVIEW");

  SemanticDelta extra;
  extra.delta_id = "graph-extra";
  extra.idempotency_key = "graph-extra";
  extra.expected_state_version = journal.state_version();
  extra.nodes = {{{"extra/G", NodeKind::Entity, "post-snapshot entity", {}}}};
  CHECK(graph.apply(extra).committed);
  CHECK(graph.set_question_memory_state("question/G", QuestionMemoryState::Active,
                                        graph.state_version(), error));

  CHECK(evidence.history().at("evidence-a").size() == 2);
  CHECK(chaos.list().size() == 2);
  CHECK(boundary.pending_claim_count() == 1);
  CHECK(questions.size() == 1);
  CHECK(graph.nodes().count("extra/G") == 1);

  // A too-small tail budget fails closed and leaves all live domains untouched.
  CHECK(!coordinator.restore_with_tail(snapshot, journal, "ctx", 0, 0,
      JournalTailLimits{1, 64}, &error));
  CHECK(evidence.history().at("evidence-a").size() == 2);
  CHECK(chaos.list().size() == 2);
  CHECK(graph.nodes().count("extra/G") == 1);

  // Restore from the checkpoint and replay the bounded committed tail.
  if (!coordinator.restore_with_tail(snapshot, journal, "ctx", 0, 0,
          JournalTailLimits{4096, 4U * 1024U * 1024U}, &error)) {
    std::cerr << "tail restore error: " << error << "\n";
    return 2;
  }
  CHECK(evidence.history().at("evidence-a").size() == 2);
  CHECK(chaos.list().size() == 2);
  ChaosCluster restored_chaos;
  CHECK(chaos.get("chaos-a", restored_chaos));
  CHECK(restored_chaos.lifecycle_history.size() == 3);
  CHECK(restored_chaos.members.at("chaos-member-a").membership_history.size() == 2);
  CHECK(restored_chaos.members.at("chaos-member-a").membership_status == "REVIEWED");
  CHECK(restored_chaos.associations.at("chaos-association-a").history.size() == 2);
  CHECK(restored_chaos.associations.at("chaos-association-a").valid_from == "2026-11-19T00:00:00Z");
  CHECK(restored_chaos.associations.at("chaos-association-a").confidence_observations.size() == 1);
  CHECK(restored_chaos.pattern_hypotheses.at("chaos-hypothesis-a").history.size() == 2);
  CHECK(restored_chaos.pattern_hypotheses.at("chaos-hypothesis-a").exclusion_reasons.at("chaos-member-b") == "insufficient independent support");
  CHECK(restored_chaos.pattern_hypotheses.at("chaos-hypothesis-a").confidence_observations.size() == 1);
  CHECK(restored_chaos.members.at("chaos-member-a").salience_observations.size() == 1);
  CHECK(restored_chaos.observations.at("novelty").size() == 1);
  TemporalBoundaryPending restored_boundary;
  CHECK(boundary.get("claim/A", restored_boundary));
  CHECK(restored_boundary.candidates.size() == 3);
  TemporalQuestion restored_question;
  CHECK(questions.get("claim/A", "VALID_FROM", restored_question));
  CHECK(restored_question.status == "UNDER_REVIEW");
  CHECK(restored_question.candidate_ids.size() == 3);
  CHECK(graph.nodes().size() == 7);
  CHECK(graph.nodes().count("extra/G") == 1);
  CHECK(graph.relations().size() == 1);
  const auto duplicate_delta = graph.apply(extra);
  CHECK(duplicate_delta.committed && duplicate_delta.duplicate);
  CHECK(graph.focus().has_value() && graph.focus()->cluster_id == "cluster/G");
  CHECK(graph.question_memory_state("question/G") == QuestionMemoryState::Active);
  CHECK(graph.question_reference_count("question/G") == 1);
  CHECK(graph.latest_claim_validity("claim/G")->validity.revision == 1);
  CHECK(graph.latest_evidence_validity("evidence/G")->validity.revision == 1);

  // Differential oracle: replay the entire journal from an independent copy.
  std::filesystem::copy_file(path, copy_path,
      std::filesystem::copy_options::overwrite_existing, ec);
  CHECK(!ec);
  EventJournal full_journal(copy_path);
  CHECK(full_journal.open());
  EvidenceProvenanceLedger full_evidence(full_journal);
  CHECK(full_evidence.replay());
  ChaosClusterLedger full_chaos(full_journal);
  CHECK(full_chaos.open());
  TemporalBoundaryLedger full_boundary(full_journal);
  CHECK(full_boundary.replay());
  TemporalQuestionLedger full_questions(full_journal);
  CHECK(full_questions.replay());
  ContextGraph full_graph(full_journal);
  CHECK(full_graph.open());

  ContextGraphSnapshotDomain full_gd(full_graph);
  EvidenceProvenanceSnapshotDomain full_ed(full_evidence);
  ChaosClusterSnapshotDomain full_cd(full_chaos);
  TemporalBoundarySnapshotDomain full_bd(full_boundary);
  TemporalQuestionSnapshotDomain full_qd(full_questions);
  SnapshotCoordinator full_coordinator;
  register_all(full_coordinator, full_gd, full_ed, full_cd, full_bd,
               full_qd, error);
  std::string tail_replayed_state, full_replayed_state;
  CHECK(coordinator.capture("ctx", journal.state_version(), journal.state_version(),
                            &tail_replayed_state, &error));
  CHECK(full_coordinator.capture("ctx", full_journal.state_version(),
                                  full_journal.state_version(),
                                  &full_replayed_state, &error));
  CHECK(tail_replayed_state == full_replayed_state);

  // A malformed tail event must not partially publish any prepared domain.
  SnapshotDocument before_failed_document;
  CHECK(SnapshotCodec::decode(tail_replayed_state, {}, &before_failed_document, &error));
  const auto bad_commit = journal.commit("tx-bad-tail", "idem-bad-tail",
      journal.state_version(),
      {JournalEvent{"bad-evidence-event", "EVIDENCE_PROVENANCE_SET_V1", "malformed"}});
  CHECK(bad_commit.committed);
  CHECK(!coordinator.restore_with_tail(snapshot, journal, "ctx", 0, 0,
      JournalTailLimits{4096, 4U * 1024U * 1024U}, &error));
  std::string after_failed_restore;
  CHECK(coordinator.capture("ctx", journal.state_version(), journal.state_version(),
                            &after_failed_restore, &error));
  SnapshotDocument after_failed_document;
  CHECK(SnapshotCodec::decode(after_failed_restore, {}, &after_failed_document, &error));
  CHECK(evidence.history().at("evidence-a").size() == 2);
  CHECK(chaos.list().size() == 2);
  CHECK(graph.nodes().count("extra/G") == 1);
  CHECK(before_failed_document.payload == after_failed_document.payload);

  std::filesystem::remove(path, ec);
  std::filesystem::remove(copy_path, ec);
  std::cout << "SURGE snapshot plus journal-tail differential tests passed\n";
  return 0;
}
