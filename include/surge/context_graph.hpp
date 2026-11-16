#pragma once
#include "surge/event_journal.hpp"
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace surge {
enum class NodeKind { Cluster, Entity, Evidence, Claim, Question, QuestionAspect, Dependency, Interest };
enum class RelationKind { Supports, Contradicts, AgreesWith, ConflictsWith, DependsOn, About, Supersedes, Retracts };
enum class ClaimStatus { Active, Superseded, Retracted };
enum class ClaimTemporalState { Unknown, NotYetValid, Valid, Expired };
enum class ScopeKind { Negation, Condition, Modality, Concession, Contrast, Coreference, Anaphora, Quotation, Temporal, ListContinuation, TableHeader };
enum class ScopeStatus { Closed, Unresolved };
enum class QuestionAspectStatus { Open, Resolved, Deferred };
enum class ScopeEffect { NegatedClaim, ConditionalRelation, ModalityQualified, ConcessionRelation, ContrastRelation, ReferenceCandidate, QuotationEvidence, TemporalQualifier, StructuralContinuation, TableHeaderScope };
struct GraphNode { std::string id; NodeKind kind{NodeKind::Cluster}; std::string content; std::vector<std::string> references; };
struct GraphRelation { std::string id; RelationKind kind{RelationKind::About}; std::string from; std::string to; };
struct FocusRecord { std::string cluster_id; std::string interest_id; };
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
struct ClaimValidity { std::string claim_id, valid_from, valid_until, basis_evidence_id; std::uint64_t revision{0}; };
struct ClaimValidityRecord { ClaimValidity validity; std::uint64_t recorded_state_version{0}; };
ClaimTemporalState evaluate_claim_validity(const ClaimValidityRecord& record, const std::string& instant);
struct SetFocus { FocusRecord focus; };
struct SemanticDelta {
  std::string delta_id, idempotency_key;
  std::uint64_t expected_state_version{0};
  std::vector<AddNode> nodes;
  std::vector<AddRelation> relations;
  std::vector<AddScopeSpan> scope_spans;
  std::optional<SetFocus> focus;
  std::vector<ClaimValidity> claim_validity_updates;
};
struct DependencyTrigger { std::string dependency_id, prerequisite_id, dependent_id; };
struct DependencyEvaluationRecord {
  std::string delta_id;
  std::vector<DependencyTrigger> triggered;
  std::vector<std::string> impacted_nodes;
  std::vector<std::string> rejected_dependencies;
};
struct DeltaResult { bool committed{false}, duplicate{false}; std::uint64_t state_version{0}; std::string error; };
class ContextGraph {
 public:
  explicit ContextGraph(std::string journal_path);
  explicit ContextGraph(EventJournal& shared_journal);
  bool open();
  DeltaResult apply(const SemanticDelta& delta);
  bool prepare_events(const SemanticDelta& delta, std::vector<JournalEvent>& events, std::string& error) const;
  std::uint64_t state_version() const { return journal_->state_version(); }
  const std::map<std::string, GraphNode>& nodes() const { return state_.nodes; }
  const std::map<std::string, GraphRelation>& relations() const { return state_.relations; }
  const std::map<std::string, ScopeSpanRecord>& scope_spans() const { return state_.scope_spans; }
  const std::optional<FocusRecord>& focus() const { return state_.focus; }
  const std::vector<FocusRecord>& focus_history() const { return state_.focus_history; }
  const std::map<std::string, DependencyEvaluationRecord>& dependency_evaluations() const {
    return state_.dependency_evaluations;
  }
  QuestionAspectStatus question_aspect_status(const std::string& id) const;
  const std::map<std::string, std::vector<ClaimValidityRecord>>& claim_validity_history() const { return state_.claim_validity_history; }
  std::optional<ClaimValidityRecord> latest_claim_validity(const std::string& id) const {
    const auto it = state_.claim_validity_history.find(id);
    if (it == state_.claim_validity_history.end() || it->second.empty()) return std::nullopt;
    return it->second.back();
  }
  ClaimTemporalState claim_validity_at(const std::string& id, const std::string& instant) const;

  ClaimStatus claim_status(const std::string& id) const {
    const auto it = state_.claim_statuses.find(id);
    return it == state_.claim_statuses.end() ? ClaimStatus::Active : it->second;
  }
  bool set_question_aspect_status(const std::string& id, QuestionAspectStatus status,
                                  std::uint64_t expected_state_version, std::string& error);
 private:
  struct State {
    std::map<std::string, GraphNode> nodes;
    std::map<std::string, GraphRelation> relations;
    std::map<std::string, ScopeSpanRecord> scope_spans;
    std::optional<FocusRecord> focus;
    std::vector<FocusRecord> focus_history;
    std::map<std::string, QuestionAspectStatus> question_aspect_statuses;
    std::map<std::string, ClaimStatus> claim_statuses;
    std::map<std::string, std::vector<ClaimValidityRecord>> claim_validity_history;
    std::map<std::string, DependencyEvaluationRecord> dependency_evaluations;
  };
  bool apply_node(State&, const GraphNode&, std::string&) const;
  bool apply_relation(State&, const GraphRelation&, std::string&) const;
  bool apply_scope_span(State&, const AddScopeSpan&, std::string&) const;
  bool apply_focus(State&, const FocusRecord&, std::string&) const;
  bool apply_claim_validity(State&, const ClaimValidity&, std::uint64_t recorded_state_version, std::string&) const;
  bool replay();
  bool prepare_transaction(const SemanticDelta&, State&, std::vector<JournalEvent>&,
                           std::string&) const;
  std::string fingerprint(const SemanticDelta&) const;
  std::vector<JournalEvent> encode(const SemanticDelta&, const std::string&) const;
  bool replay_event(State&, const JournalEvent&, std::string&) const;
  std::unique_ptr<EventJournal> owned_journal_;
  EventJournal* journal_{nullptr};
  State state_;
  std::map<std::string, std::string> idempotency_;
  std::string last_error_;
};
}  // namespace surge
