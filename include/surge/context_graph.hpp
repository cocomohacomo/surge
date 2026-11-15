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
enum class RelationKind { Supports, Contradicts, AgreesWith, ConflictsWith, DependsOn, About };
enum class ScopeKind { Negation, Condition, Modality, Concession, Contrast, Coreference, Anaphora, Quotation, Temporal, ListContinuation, TableHeader };
enum class ScopeStatus { Closed, Unresolved };
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
struct AddNode { GraphNode node; };
struct AddRelation { GraphRelation relation; };
struct AddScopeSpan { ScopeSpanProposal span; std::string source_snapshot; };
struct SetFocus { FocusRecord focus; };
struct SemanticDelta {
  std::string delta_id, idempotency_key;
  std::uint64_t expected_state_version{0};
  std::vector<AddNode> nodes;
  std::vector<AddRelation> relations;
  std::vector<AddScopeSpan> scope_spans;
  std::optional<SetFocus> focus;
};
struct DeltaResult { bool committed{false}, duplicate{false}; std::uint64_t state_version{0}; std::string error; };
class ContextGraph {
 public:
  explicit ContextGraph(std::string journal_path);
  explicit ContextGraph(EventJournal& shared_journal);
  bool open();
  DeltaResult apply(const SemanticDelta& delta);
  std::uint64_t state_version() const { return journal_->state_version(); }
  const std::map<std::string, GraphNode>& nodes() const { return state_.nodes; }
  const std::map<std::string, GraphRelation>& relations() const { return state_.relations; }
  const std::map<std::string, ScopeSpanProposal>& scope_spans() const { return state_.scope_spans; }
  const std::optional<FocusRecord>& focus() const { return state_.focus; }
  const std::vector<FocusRecord>& focus_history() const { return state_.focus_history; }
 private:
  struct State {
    std::map<std::string, GraphNode> nodes;
    std::map<std::string, GraphRelation> relations;
    std::map<std::string, ScopeSpanProposal> scope_spans;
    std::optional<FocusRecord> focus;
    std::vector<FocusRecord> focus_history;
  };
  bool apply_node(State&, const GraphNode&, std::string&) const;
  bool apply_relation(State&, const GraphRelation&, std::string&) const;
  bool apply_scope_span(State&, const AddScopeSpan&, std::string&) const;
  bool apply_focus(State&, const FocusRecord&, std::string&) const;
  bool replay();
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
