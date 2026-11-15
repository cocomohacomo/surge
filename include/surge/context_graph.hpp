#pragma once
#include "surge/event_journal.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace surge {
enum class NodeKind { Cluster, Entity, Evidence, Claim, Question, QuestionAspect, Dependency, Interest };
enum class RelationKind { Supports, Contradicts, AgreesWith, ConflictsWith, DependsOn, About };
struct GraphNode {
  std::string id;
  NodeKind kind{NodeKind::Cluster};
  std::string content;
  std::vector<std::string> references;
};
struct GraphRelation {
  std::string id;
  RelationKind kind{RelationKind::About};
  std::string from;
  std::string to;
};
struct FocusRecord {
  std::string cluster_id;
  std::string interest_id;
};
struct AddNode { GraphNode node; };
struct AddRelation { GraphRelation relation; };
struct SetFocus { FocusRecord focus; };
struct SemanticDelta {
  std::string delta_id;
  std::string idempotency_key;
  std::uint64_t expected_state_version{0};
  std::vector<AddNode> nodes;
  std::vector<AddRelation> relations;
  std::optional<SetFocus> focus;
};
struct DeltaResult {
  bool committed{false};
  bool duplicate{false};
  std::uint64_t state_version{0};
  std::string error;
};

class ContextGraph {
 public:
  explicit ContextGraph(std::string journal_path);
  bool open();
  DeltaResult apply(const SemanticDelta& delta);
  std::uint64_t state_version() const { return journal_.state_version(); }
  const std::map<std::string, GraphNode>& nodes() const { return nodes_; }
  const std::map<std::string, GraphRelation>& relations() const { return relations_; }
  const std::optional<FocusRecord>& focus() const { return focus_; }
  const std::vector<FocusRecord>& focus_history() const { return focus_history_; }
 private:
  struct State {
    std::map<std::string, GraphNode> nodes;
    std::map<std::string, GraphRelation> relations;
    std::optional<FocusRecord> focus;
    std::vector<FocusRecord> focus_history;
  };
  bool apply_node(State& state, const GraphNode& node, std::string& error) const;
  bool apply_relation(State& state, const GraphRelation& relation, std::string& error) const;
  bool apply_focus(State& state, const FocusRecord& focus, std::string& error) const;
  bool replay();
  std::string fingerprint(const SemanticDelta& delta) const;
  std::vector<JournalEvent> encode(const SemanticDelta& delta, const std::string& fingerprint) const;
  bool replay_event(State& state, const JournalEvent& event, std::string& error) const;
  EventJournal journal_;
  State state_;
  std::map<std::string, std::string> idempotency_;
};
}  // namespace surge
