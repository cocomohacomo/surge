#pragma once
#include "surge/context_graph.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
namespace surge {
// Semantic candidates, priorities, and target clusters are external Projector inputs.
// This policy only schedules Focus from those explicit inputs.
struct QuestionInterestFocusInput {
 std::string question_id, interest_id, target_cluster_id;
 double priority{0.0}; bool active{true}; SpatialVector3 target_position;
};
struct QuestionInterestFocusDecision {
 bool changed{false}; std::string selected_question_id, selected_interest_id, target_cluster_id;
 double priority{0.0}; FocusRegion region;
};
class QuestionInterestFocusController {
 public:
 explicit QuestionInterestFocusController(SpatialVector3 half_extents={1.0,1.0,1.0}):half_extents_(half_extents){}
 QuestionInterestFocusDecision evaluate(const std::vector<QuestionInterestFocusInput>& inputs) const {
  std::vector<QuestionInterestFocusInput> eligible;
  for(const auto& x:inputs) if(x.active&&!x.question_id.empty()&&!x.interest_id.empty()&&!x.target_cluster_id.empty()&&
      std::isfinite(x.priority)&&std::isfinite(x.target_position.x)&&
      std::isfinite(x.target_position.y)&&std::isfinite(x.target_position.z)&&
      valid_half_extents()) eligible.push_back(x);
  std::stable_sort(eligible.begin(),eligible.end(),[](const auto&a,const auto&b){
   if(a.priority!=b.priority)return a.priority>b.priority;
   if(a.question_id!=b.question_id)return a.question_id<b.question_id;
   return a.interest_id<b.interest_id;});
  QuestionInterestFocusDecision d; if(eligible.empty())return d;
  const auto&w=eligible.front(); d.changed=true;d.selected_question_id=w.question_id;d.selected_interest_id=w.interest_id;
  d.target_cluster_id=w.target_cluster_id;d.priority=w.priority;d.region={w.target_position,half_extents_};return d;
 }
 DeltaResult apply(ContextGraph& graph,const std::vector<QuestionInterestFocusInput>& inputs,const std::string& delta_id) const {
  auto d=evaluate(inputs);if(!d.changed)return {false,false,graph.state_version(),"NO_ACTIVE_QUESTION_INTEREST"};
  SemanticDelta delta;delta.delta_id=delta_id;delta.idempotency_key=delta_id;delta.expected_state_version=graph.state_version();
  delta.focus_region=SetFocusRegion{d.region};delta.cluster_position_updates.push_back({d.target_cluster_id,d.region.center});
  return graph.apply(delta);
 }
 private:
 bool valid_half_extents() const noexcept {
  return std::isfinite(half_extents_.x)&&std::isfinite(half_extents_.y)&&
      std::isfinite(half_extents_.z)&&half_extents_.x>0.0&&
      half_extents_.y>0.0&&half_extents_.z>0.0;
 }
 SpatialVector3 half_extents_;
};
} // namespace surge
