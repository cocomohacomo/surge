#pragma once
#include "surge/context_graph.hpp"
#include <string>
#include <vector>

namespace surge {
struct QuestionRouteResult {
  bool committed{false};
  bool duplicate{false};
  bool focus_selected{false};
  std::uint64_t state_version{0};
  std::string question_id;
  std::string question_aspect_id;
  std::vector<std::string> interest_ids;
  std::string error;
};
class QuestionRouter {
 public:
  // Convert an explicit CLAIM↔CLAIM contradiction into a Question and
  // QuestionAspect. Interest is directed to every linked Cluster. Focus is
  // selected only when the contradiction maps unambiguously to one Cluster.
  static QuestionRouteResult route(ContextGraph& graph,
                                   const std::string& contradiction_relation_id,
                                   const std::string& policy_version);
};
}  // namespace surge
