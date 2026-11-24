#include "test_support.hpp"
#include "surge/question_interest_focus_controller.hpp"
#include <filesystem>
int main(){
 const auto path=(std::filesystem::temp_directory_path()/"surge-question-interest-focus-controller.sgj").string();
 std::filesystem::remove(path); surge::ContextGraph graph(path); CHECK(graph.open());
 surge::QuestionInterestFocusController controller({2.0,1.5,1.0});
 std::vector<surge::QuestionInterestFocusInput> inputs={
  {"q-old","i-old","cluster-old",0.2,true,{0,0,0}},
  {"q-new","i-new","cluster-new",0.9,true,{4,2,1}},
  {"q-closed","i-closed","cluster-closed",1.0,false,{9,9,9}},
  {"","","cluster-invalid",2.0,true,{8,8,8}}
 };
 auto d=controller.evaluate(inputs); CHECK(d.changed); CHECK(d.selected_question_id=="q-new");
 CHECK(d.selected_interest_id=="i-new"); CHECK(d.target_cluster_id=="cluster-new");
 CHECK(d.priority==0.9); CHECK(d.region.center.x==4.0); CHECK(d.region.half_extents.y==1.5);
 auto applied=controller.apply(graph,inputs,"focus-policy-1"); CHECK(applied.committed);
 auto obs=graph.observe_cognition(); CHECK(obs.focus_region.has_value());
 CHECK(obs.focus_region->center.x==4.0); CHECK(obs.focus_region->center.y==2.0);
 auto empty=controller.evaluate({}); CHECK(!empty.changed);
 auto inactive=controller.evaluate({{"q","i","cluster",1.0,false,{1,2,3}}}); CHECK(!inactive.changed);
 auto tie=controller.evaluate({{"q-b","i-b","c-b",0.5,true,{1,0,0}},{"q-a","i-a","c-a",0.5,true,{2,0,0}}});
 CHECK(tie.target_cluster_id=="c-a");
 std::filesystem::remove(path); return 0;
}
