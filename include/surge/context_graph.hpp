#pragma once
#include "surge/event_journal.hpp"
#include "surge/sha256.hpp"
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>

namespace surge {
enum class NodeKind { Cluster, ChaosCluster, Entity, Evidence, Claim, Question, QuestionAspect, Dependency, Interest, ClusterLifecycle };
enum class ClusterLifecycleState { Active, Dormant, Archived, Tombstoned };
struct ClusterLifecycleRecord { std::string cluster_id; ClusterLifecycleState state{ClusterLifecycleState::Active}; std::uint64_t revision{0}; std::string recorded_at, reason, provenance_digest; };
enum class RelationKind { Supports, Contradicts, AgreesWith, ConflictsWith, DependsOn, About, Supersedes, Retracts, Duplicates, Contains, DerivedFrom };
enum class ClaimStatus { Active, Superseded, Retracted };
enum class ClaimTemporalState { Unknown, NotYetValid, Valid, Expired };
enum class ScopeKind { Negation, Condition, Modality, Concession, Contrast, Coreference, Anaphora, Quotation, Temporal, ListContinuation, TableHeader };
enum class ScopeStatus { Closed, Unresolved };
enum class QuestionAspectStatus { Open, Resolved, Deferred };
enum class QuestionMemoryState { Active, Dormant, Archived };
enum class ScopeEffect { NegatedClaim, ConditionalRelation, ModalityQualified, ConcessionRelation, ContrastRelation, ReferenceCandidate, QuotationEvidence, TemporalQualifier, StructuralContinuation, TableHeaderScope };
struct GraphNode { std::string id; NodeKind kind{NodeKind::Cluster}; std::string content; std::vector<std::string> references; };
struct GraphRelation {
  std::string id;
  RelationKind kind{RelationKind::About};
  std::string from;
  std::string to;
  // For Contains edges this is the upstream classifier's membership weight.
  // SURGE stores and transports it; it never recalculates or normalizes it.
  double weight{1.0};
};
struct FocusRecord { std::string cluster_id; std::string interest_id; bool chaos_cluster{false}; };
// Spatial focus is a region in cognition graph space, not a selected cluster.
struct SpatialVector3 { double x{0.0}, y{0.0}, z{0.0}; };
struct FocusRegion { SpatialVector3 center; SpatialVector3 half_extents; };
struct ClusterSpatialPosition { std::string cluster_id; SpatialVector3 position; };
struct ClusterFocusDepth { std::string cluster_id; std::size_t depth{0}; };
struct SetFocusRegion { FocusRegion region; };
struct MoveFocusRegion { SpatialVector3 center; };
struct ResizeFocusRegion { SpatialVector3 half_extents; };
struct SetClusterSpatialPosition { ClusterSpatialPosition position; };
struct SetClusterFocusDepth { ClusterFocusDepth focus_depth; };
struct FocusWorkMetrics {
  std::size_t clusters_considered{0}, clusters_in_region{0};
  std::size_t clusters_scheduled{0}, clusters_deferred{0};
  std::size_t position_checks{0}, nodes_processed{0}, relations_examined{0}, relations_traversed{0}, relations_processed{0};
  std::size_t work_units{0}, elapsed_microseconds{0};
  std::size_t max_nodes{0}, max_relations{0};
  bool budget_exhausted{false};
};
struct ScopeAnchor {
  std::string anchor_id, source_digest, offset_basis;
  std::size_t start_offset{0}, end_offset{0};
  std::string exact_text, packet_ref;
};
struct ScopeSpanProposal {
  std::string span_id;
  ScopeKind kind{ScopeKind::Negation};
  std::string source_digest, offset_basis;
  ScopeAnchor opener;
  std::optional<ScopeAnchor> closer;
  std::vector<std::string> target_refs, antecedent_refs;
  std::string attribution_ref, modality_kind, temporal_role, parent_ref;
  std::vector<std::string> packet_refs;
};
struct ScopeValidationResult { bool accepted{false}; ScopeStatus status{ScopeStatus::Unresolved}; ScopeEffect effect{ScopeEffect::ReferenceCandidate}; bool may_emit_unconditional_claim{false}; std::string error; };
struct ScopeSpanRecord { ScopeSpanProposal proposal; ScopeStatus status{ScopeStatus::Unresolved}; ScopeEffect effect{ScopeEffect::ReferenceCandidate}; bool may_emit_unconditional_claim{false}; };
struct AddNode { GraphNode node; };
struct AddRelation { GraphRelation relation; };
struct AddScopeSpan { ScopeSpanProposal span; std::string source_snapshot; };
struct ClaimValidity { std::string claim_id, valid_from, valid_until, basis_evidence_id; std::uint64_t revision{0}; std::string review_evidence_id{}, reviewer_id{}; };
struct ClaimValidityRecord { ClaimValidity validity; std::uint64_t recorded_state_version{0}; };
struct EvidenceValidity { std::string evidence_id, valid_from, valid_until, basis_evidence_id; std::uint64_t revision{0}; std::string review_evidence_id{}, reviewer_id{}; };
struct EvidenceValidityRecord { EvidenceValidity validity; std::uint64_t recorded_state_version{0}; };
ClaimTemporalState evaluate_claim_validity(const ClaimValidityRecord& record, const std::string& instant);
struct SetFocus { FocusRecord focus; };
struct SemanticDelta {
  std::string delta_id, idempotency_key;
  std::uint64_t expected_state_version{0};
  std::vector<AddNode> nodes;
  std::vector<AddRelation> relations;
  // Append-only journal command that removes active Contains edges without deleting members.
  std::vector<std::string> remove_relation_ids;
  // Explicitly reclaim eligible unreferenced Entity payload nodes.
  std::vector<std::string> reclaim_entity_node_ids;
  std::vector<AddScopeSpan> scope_spans;
  std::optional<SetFocus> focus;
  std::optional<SetFocusRegion> focus_region;
  std::optional<MoveFocusRegion> focus_region_move;
  std::optional<ResizeFocusRegion> focus_region_resize;
  std::vector<SetClusterSpatialPosition> cluster_position_updates;
  std::vector<SetClusterFocusDepth> cluster_focus_depth_updates;
  std::vector<ClaimValidity> claim_validity_updates;
  std::vector<EvidenceValidity> evidence_validity_updates;
};
struct DependencyTrigger { std::string dependency_id, prerequisite_id, dependent_id; };
struct DependencyEvaluationRecord {
  std::string delta_id;
  std::vector<DependencyTrigger> triggered;
  std::vector<std::string> impacted_nodes;
  std::vector<std::string> rejected_dependencies;
};
struct ReassessmentEvidenceSnapshot {
  std::string evidence_id, relation_id;
  RelationKind relation{RelationKind::Supports};
  std::uint64_t validity_revision{0}, validity_recorded_state_version{0};
  std::string valid_from, valid_until, basis_evidence_id;
  ClaimTemporalState temporal_state{ClaimTemporalState::Unknown};
  bool eligible{false};
};
struct TemporalReassessmentRecord {
  std::string trace_id, claim_id, reference_at, decision;
  std::string claim_valid_from, claim_valid_until, claim_validity_basis_evidence_id, reason_code;
  std::uint64_t claim_validity_revision{0}, claim_validity_recorded_state_version{0};
  std::uint64_t evaluated_state_version{0}, recorded_state_version{0}, sequence{0};
  ClaimStatus lifecycle{ClaimStatus::Active};
  ClaimTemporalState claim_temporal_state{ClaimTemporalState::Unknown};
  std::vector<ReassessmentEvidenceSnapshot> evidence;
  std::vector<std::string> active_support_ids, active_contradiction_ids;
  std::string previous_hash, snapshot_digest, chain_hash;
};
struct ObservedMembership {
  std::string member_node_id;
  std::string cluster_node_id;
  double upstream_weight{1.0};
};
struct CognitionObservation {
  std::uint64_t state_version{0};
  std::size_t next_root_index{0};
  bool complete{false};
  std::vector<GraphNode> nodes;
  std::vector<GraphRelation> relations;
  std::vector<ObservedMembership> memberships;
  std::vector<std::string> unresolved_member_ids;
  std::vector<std::string> active_question_ids;
  std::vector<std::string> active_interest_ids;
  std::optional<FocusRecord> current_focus;
  std::vector<FocusRecord> focus_history;
  std::optional<FocusRegion> focus_region;
  std::vector<ClusterFocusDepth> cluster_focus_depths;
  std::vector<ClusterSpatialPosition> cluster_spatial_positions;
  std::vector<std::string> logical_region_cluster_ids;
  std::vector<std::string> processed_region_cluster_ids;
  std::vector<std::string> deferred_region_cluster_ids;
  FocusWorkMetrics focus_work;
  std::size_t unresolved_scope_count{0};
  std::size_t dependency_evaluation_count{0};
  // Stable cursor invalidation reason; empty for valid observations.
  std::string error;
  // Snapshot semantics: detached copies, safe for external read-only inspection.
};
struct CognitionCandidate {
  std::string cluster_id;
  std::vector<std::string> supporting_member_ids;
  std::vector<std::string> matched_terms;
  double recall_score{0.0}; // Recall-only score; never a Contains weight.
  std::uint64_t memory_state_version{0};
};
struct CognitionStateSnapshot {
  std::string snapshot_id, cluster_id, input_member_id;
  std::uint64_t evaluated_state_version{0};
  double recall_score{0.0};
  std::vector<std::string> supporting_member_ids, matched_terms;
};
// Caller-owned continuation state for traversal inside a logical Focus root.
struct FocusTraversalFrontierEntry {
  std::string cluster_id, origin_root_id;
  std::size_t level{0};
  // Next outgoing adjacency slot to inspect when a node is suspended by the
  // relation budget. Keeping this offset avoids rescanning already-accounted
  // edges when the caller resumes the same traversal.
  std::size_t next_relation_index{0};
};
struct FocusTraversalCursor {
  std::uint32_t schema_version{1};
  std::uint64_t state_version{0};
  std::string query_digest, focus_digest;
  std::size_t next_root_index{0};
  std::string active_root_id;
  std::vector<FocusTraversalFrontierEntry> frontier;
  // Expansion state is origin-root scoped to preserve independent depth limits.
  std::vector<std::string> visited_expansion_keys;
  std::vector<std::string> inspected_relation_ids;
  // Materialization state is retained separately from expansion state so a
  // shared node can be emitted once while still being expanded per origin root.
  std::vector<std::string> visible_node_ids;
  std::vector<std::string> completed_root_ids;
  bool initialized{false}, complete{false};
};
struct FocusedCognitionCursor {
  std::uint64_t state_version{0};
  std::string query_digest;
  std::size_t next_root_index{0};
  bool initialized{false};
};
struct FocusedCognitionCandidates {
  std::uint64_t state_version{0};
  std::size_t next_root_index{0};
  bool complete{false};
  std::string error;
  std::vector<std::string> logical_region_cluster_ids;
  std::vector<std::string> processed_region_cluster_ids;
  std::vector<std::string> deferred_region_cluster_ids;
  std::vector<CognitionCandidate> candidates;
  std::vector<CognitionCandidate> deferred_candidates;
  FocusWorkMetrics focus_work;
};
struct CognitionFeedback {
  std::string feedback_id, candidate_cluster_id, input_member_id;
  std::string outcome; // CONFIRMED, REJECTED, UNRESOLVED, REVISED
  std::string producer_id, producer_version, policy_version, recorded_at;
  std::string notes;
};
struct DeltaResult { bool committed{false}, duplicate{false}; std::uint64_t state_version{0}; std::string error; };
class ContextGraphSnapshotDomain;
class PreparedContextGraphSnapshotRestore;
class BudgetedContextGraph;
class ClusterArchiveGraphRestorer;
class ContextGraph {
 public:
  explicit ContextGraph(std::string journal_path);
  explicit ContextGraph(EventJournal& shared_journal);
  bool open();
  DeltaResult apply(const SemanticDelta& delta);
  // Core-owned classification: persist a member beneath a deterministic typed
  // child cluster, creating the cluster and parent edge in the same delta.
  bool append_entity_reclamation(SemanticDelta& delta,
      const std::string& entity_node_id, std::string& error) const;
  bool append_cluster_unmembership(SemanticDelta& delta,
      const std::string& cluster_id, const std::string& member_node_id,
      std::string& error) const;
  // Open-world admission: retain an unclassified member in a stable
  // ChaosCluster without inventing semantic relations. Later classification
  // may add memberships and explicitly remove this provisional membership.
  bool append_unresolved_membership(SemanticDelta& delta,
      const std::string& member_node_id, std::string& chaos_cluster_id,
      std::string& membership_relation_id, std::string& error) const;
  bool append_cluster_membership(SemanticDelta& delta,
      const std::string& parent_cluster_id, const std::string& classification,
      const std::string& partition_key, const std::string& member_node_id,
      const std::string& relation_prefix, std::string& assigned_cluster_id,
      std::string& error, double membership_weight = 1.0) const;
  bool reassess_claim(const std::string& claim_id, const std::string& reference_at,
                      std::uint64_t expected_state_version,
                      TemporalReassessmentRecord& result, std::string& error);
  const std::map<std::string, std::vector<TemporalReassessmentRecord>>& reassessment_history() const {
    return state_.reassessment_history;
  }
  bool prepare_events(const SemanticDelta& delta, std::vector<JournalEvent>& events, std::string& error) const;
  std::uint64_t state_version() const { return journal_->state_version(); }
  // Read-only projection of the exact committed cognition state; no inference or mutation.
  CognitionObservation observe_cognition() const;
  // Read-only projection containing only the focused cognition cluster and its direct members.
  CognitionObservation observe_focused_cognition() const;
  CognitionObservation observe_focused_cognition(std::size_t max_nodes, std::size_t max_relations, std::size_t start_root_index = 0) const;
  bool append_focus_region_update(SemanticDelta& delta, const FocusRegion& region, std::string& error) const;
  bool append_focus_region_move(SemanticDelta& delta, const SpatialVector3& center, std::string& error) const;
  bool append_focus_region_resize(SemanticDelta& delta, const SpatialVector3& half_extents, std::string& error) const;
  bool append_cluster_spatial_position_update(SemanticDelta& delta, const ClusterSpatialPosition& position, std::string& error) const;
  bool append_cluster_focus_depth_update(SemanticDelta& delta, const ClusterFocusDepth& depth, std::string& error) const;
  // Spatial projection with independent region geometry and per-cluster depth.
  // Depth 0 exposes the cluster only; each level traverses one Contains edge.
  // Budgets cap node/relation work deterministically.
  CognitionObservation observe_focused_cognition(
      const FocusRegion& region,
      const std::vector<ClusterSpatialPosition>& positions,
      const std::vector<ClusterFocusDepth>& depths,
      std::size_t max_nodes = 10000,
      std::size_t max_relations = 20000,
      std::size_t start_root_index = 0) const;
  // Resumable observation. The caller owns the cursor; budgets apply per call.
  // A cursor is rejected when the committed graph or logical Focus changes.
  CognitionObservation observe_focused_cognition(
      const FocusRegion& region,
      const std::vector<ClusterSpatialPosition>& positions,
      const std::vector<ClusterFocusDepth>& depths,
      FocusTraversalCursor& cursor,
      std::size_t max_nodes = 10000,
      std::size_t max_relations = 20000) const;
  // Add a new cognition cluster with explicit lineage to the activated source clusters.
  // The caller supplies the recognized content; Core validates and journals the structure.
  bool append_derived_cognition_cluster(SemanticDelta& delta,
      const std::string& cluster_id, const std::string& content,
      const std::vector<std::string>& source_cluster_ids,
      const std::string& relation_prefix, std::string& error) const;
  // Deterministic lightweight associative recall. Query terms are supplied by
  // the caller; Core does not classify the input. Scores are retrieval-only.
  std::vector<CognitionCandidate> recall_cognition_candidates(
      const std::string& input_member_id, const std::vector<std::string>& query_terms,
      std::size_t max_candidates = 8) const;
  // Recall is restricted to clusters materialized by the current Focus work
  // budget. Candidates in logical but unprocessed roots are reported deferred.
  // The logical Focus region itself is never reduced to fit the budget.
  FocusedCognitionCandidates recall_focused_cognition_candidates(
      const std::string& input_member_id, const std::vector<std::string>& query_terms,
      std::size_t max_candidates = 8, std::size_t max_nodes = 10000,
      std::size_t max_relations = 20000) const;
  // Resumes at a deterministic Focus root boundary. The caller owns the cursor;
  // it is valid only for the same committed graph version and query digest.
  FocusedCognitionCandidates recall_focused_cognition_candidates(
      const std::string& input_member_id, const std::vector<std::string>& query_terms,
      FocusedCognitionCursor& cursor, std::size_t max_candidates = 8,
      std::size_t max_nodes = 10000, std::size_t max_relations = 20000) const;
  // Append an immutable, provenance-linked snapshot of a candidate evaluation.
  // The candidate must have been recalled from the transaction's expected graph version.
  bool append_cognition_state_snapshot(SemanticDelta& delta,
      const CognitionCandidate& candidate, const std::string& input_member_id,
      const std::string& snapshot_node_id, const std::string& relation_prefix,
      std::string& error) const;
  // Append externally evaluated candidate feedback to a caller-owned transaction.
  bool append_cognition_feedback(SemanticDelta& delta,
      const CognitionFeedback& feedback, const std::string& feedback_node_id,
      const std::string& relation_id, std::string& error) const;
  // Safely encode and append one lifecycle transition plus its required
  // cluster membership edge to a caller-owned delta. No graph mutation occurs.
  bool append_cluster_lifecycle(SemanticDelta& delta,
      const ClusterLifecycleRecord& record, const std::string& lifecycle_node_id,
      const std::string& relation_id, std::string& error) const;
  std::optional<ClusterLifecycleRecord> cluster_lifecycle(const std::string& cluster_id) const;
  const std::map<std::string, GraphNode>& nodes() const { return state_.nodes; }
  const std::map<std::string, GraphRelation>& relations() const { return state_.relations; }
  // Counts active Cluster/ChaosCluster Contains references to a canonical member.
  std::size_t cluster_membership_reference_count(const std::string& member_node_id) const;
  const std::map<std::string, ScopeSpanRecord>& scope_spans() const { return state_.scope_spans; }
  const std::optional<FocusRecord>& focus() const { return state_.focus; }
  const std::vector<FocusRecord>& focus_history() const { return state_.focus_history; }
  const std::map<std::string, DependencyEvaluationRecord>& dependency_evaluations() const {
    return state_.dependency_evaluations;
  }
  QuestionAspectStatus question_aspect_status(const std::string& id) const;
  QuestionMemoryState question_memory_state(const std::string& id) const;
  std::uint64_t question_reference_count(const std::string& id) const;
  bool record_question_reference(const std::string& id, std::uint64_t expected_state_version, bool& reactivated, std::string& error);
  bool record_question_reference_with_dependents(const std::string& id, std::uint64_t expected_state_version, std::size_t max_depth, std::size_t max_nodes, bool& reactivated, std::size_t& reactivated_dependents, std::string& error);
  bool set_question_memory_state(const std::string& id, QuestionMemoryState state, std::uint64_t expected_state_version, std::string& error);
  bool enforce_question_memory_capacity(const std::vector<std::pair<std::string, double>>& scores, std::size_t max_active, std::uint64_t expected_state_version, std::vector<std::string>& demoted, std::string& error);
  const std::map<std::string, std::vector<ClaimValidityRecord>>& claim_validity_history() const { return state_.claim_validity_history; }
  std::optional<ClaimValidityRecord> latest_claim_validity(const std::string& id) const {
    const auto it = state_.claim_validity_history.find(id);
    if (it == state_.claim_validity_history.end() || it->second.empty()) return std::nullopt;
    return it->second.back();
  }
  ClaimTemporalState claim_validity_at(const std::string& id, const std::string& instant) const;
  const std::map<std::string, std::vector<EvidenceValidityRecord>>& evidence_validity_history() const { return state_.evidence_validity_history; }
  std::optional<EvidenceValidityRecord> latest_evidence_validity(const std::string& id) const {
    const auto it = state_.evidence_validity_history.find(id);
    if (it == state_.evidence_validity_history.end() || it->second.empty()) return std::nullopt;
    return it->second.back();
  }
  ClaimTemporalState evidence_validity_at(const std::string& id, const std::string& instant) const;

  ClaimStatus claim_status(const std::string& id) const {
    const auto it = state_.claim_statuses.find(id);
    return it == state_.claim_statuses.end() ? ClaimStatus::Active : it->second;
  }
  bool set_question_aspect_status(const std::string& id, QuestionAspectStatus status,
                                  std::uint64_t expected_state_version, std::string& error);
  bool apply_question_aspect_statuses(const std::vector<std::pair<std::string, QuestionAspectStatus>>& updates,
      std::uint64_t expected_state_version, std::string& error);
  // Durable, merge-only import of a fully validated ClusterArchive v2 payload.
  // The complete import is one journal transaction and is replayed after restart.
  DeltaResult import_cluster_archive_payload(const std::string& payload,
      std::uint64_t expected_state_version, const std::string& transaction_id,
      const std::string& idempotency_key);
 private:
  friend class BudgetedContextGraph;
  friend class ContextGraphSnapshotDomain;
  friend class PreparedContextGraphSnapshotRestore;
  friend class ClusterArchiveGraphRestorer;
  struct State {
    std::map<std::string, GraphNode> nodes;
    std::map<std::string, GraphRelation> relations;
    std::map<std::string, ScopeSpanRecord> scope_spans;
    std::optional<FocusRecord> focus;
    std::vector<FocusRecord> focus_history;
    std::optional<FocusRegion> focus_region;
    std::map<std::string, SpatialVector3> cluster_positions;
    std::map<std::string, std::size_t> cluster_focus_depths;
    std::map<std::string, QuestionAspectStatus> question_aspect_statuses;
    std::map<std::string, QuestionMemoryState> question_memory_states;
    std::map<std::string, std::uint64_t> question_reference_counts;
    std::map<std::string, ClaimStatus> claim_statuses;
    std::map<std::string, std::vector<ClaimValidityRecord>> claim_validity_history;
    std::map<std::string, std::vector<EvidenceValidityRecord>> evidence_validity_history;
    std::map<std::string, DependencyEvaluationRecord> dependency_evaluations;
    std::map<std::string, std::vector<TemporalReassessmentRecord>> reassessment_history;
  };
  // Shared implementation for legacy root paging and resumable Focus traversal.
  // The legacy API remains a thin wrapper; cursor-aware entry points reuse this
  // engine rather than maintaining a second graph-walk implementation.
  CognitionObservation observe_focused_cognition_impl(
      const FocusRegion& region,
      const std::vector<ClusterSpatialPosition>& positions,
      const std::vector<ClusterFocusDepth>& depths,
      std::size_t max_nodes, std::size_t max_relations,
      std::size_t start_root_index, FocusTraversalCursor* cursor = nullptr) const;
  bool apply_node(State&, const GraphNode&, std::string&) const;
  bool apply_relation(State&, const GraphRelation&, std::string&) const;
  bool apply_relation_removal(State&, const std::string&, std::string&) const;
  bool apply_entity_reclamation(State&, const std::string&, std::string&) const;
  bool apply_scope_span(State&, const AddScopeSpan&, std::string&) const;
  bool apply_focus(State&, const FocusRecord&, std::string&) const;
  bool apply_focus_region(State&, const FocusRegion&, std::string&) const;
  bool apply_focus_region_move(State&, const SpatialVector3&, std::string&) const;
  bool apply_focus_region_resize(State&, const SpatialVector3&, std::string&) const;
  bool apply_cluster_position(State&, const ClusterSpatialPosition&, std::string&) const;
  bool apply_cluster_depth(State&, const ClusterFocusDepth&, std::string&) const;
  bool apply_claim_validity(State&, const ClaimValidity&, std::uint64_t recorded_state_version, std::string&) const;
  bool apply_evidence_validity(State&, const EvidenceValidity&, std::uint64_t recorded_state_version, std::string&) const;
  bool build_reassessment(const State&, const std::string&, const std::string&,
      std::uint64_t, TemporalReassessmentRecord&, std::string&) const;
  bool apply_reassessment(State&, const TemporalReassessmentRecord&, std::string&) const;
  bool replay();
  bool prepare_transaction(const SemanticDelta&, State&, std::vector<JournalEvent>&,
                           std::string&) const;
  std::string fingerprint(const SemanticDelta&) const;
  std::vector<JournalEvent> encode(const SemanticDelta&, const std::string&) const;
  bool replay_event(State&, const JournalEvent&, std::string&) const;
  std::unique_ptr<EventJournal> owned_journal_;
  EventJournal* journal_{nullptr};
  State state_;
  // Derived Recall index: rebuilt on committed state-version changes and never journaled.
  mutable std::uint64_t recall_index_state_version_{~std::uint64_t{0}};
  mutable std::map<std::string, std::vector<std::pair<std::pair<std::string, std::string>, double>>> recall_term_postings_;
  mutable std::map<std::string, std::vector<std::pair<std::pair<std::string, std::string>, double>>> recall_member_postings_;
  // Derived one-axis spatial index over persisted Cluster positions. It is
  // rebuilt only when position state is replaced and is never journaled.
  mutable bool spatial_position_index_valid_{false};
  mutable std::map<double, std::vector<std::string>> spatial_position_x_index_;
  // Derived outgoing adjacency stores IDs, never pointers into mutable graph state.
  mutable bool spatial_relation_index_valid_{false};
  mutable std::map<std::string, std::vector<std::string>> spatial_outgoing_relation_ids_;
  std::map<std::string, std::string> idempotency_;
  std::string last_error_;
};
}  // namespace surge
