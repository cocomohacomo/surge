#include "surge/c_api.h"
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

int main() {
  const auto path=(std::filesystem::temp_directory_path()/"surge_spatial_focus_c_api.journal").string();
  std::error_code ec;std::filesystem::remove(path,ec);
  char error[256]={};
  void* handle=surge_packet_core_open(path.c_str(),error,sizeof(error));
  assert(handle!=nullptr);
  const SurgeGraphNodeInput nodes[]={{"focus-cluster","CLUSTER","focus topic",nullptr,0}};
  int duplicate=0;uint64_t version=0;
  assert(surge_context_graph_apply(handle,"seed-focus","seed-focus",0,nodes,1,
      nullptr,0,nullptr,nullptr,&duplicate,&version,error,sizeof(error))==1);
  assert(version==1);
  const SurgeFocusRegionInput region{0.0,0.0,0.0,2.0,2.0,2.0};
  assert(surge_context_graph_set_focus_region(handle,"set-region","set-region",1,
      &region,&duplicate,&version,error,sizeof(error))==1);
  assert(version==2);
  const SurgeClusterFocusDepthInput depth{"focus-cluster",3};
  assert(surge_context_graph_set_cluster_focus_depth(handle,"set-depth","set-depth",
      2,&depth,&duplicate,&version,error,sizeof(error))==1);
  assert(version==3);
  const SurgeFocusRegionCenterInput moved_center{1.0,0.0,0.0};
  assert(surge_context_graph_move_focus_region(handle,"move-region","move-region",3,
      &moved_center,&duplicate,&version,error,sizeof(error))==1);
  assert(version==4);
  const SurgeFocusRegionExtentInput resized_extents{1.0,1.0,1.0};
  assert(surge_context_graph_resize_focus_region(handle,"resize-region","resize-region",4,
      &resized_extents,&duplicate,&version,error,sizeof(error))==1);
  assert(version==5);
  const SurgeClusterSpatialPositionInput position{"focus-cluster",0.5,0.0,0.0};
  assert(surge_context_graph_set_cluster_spatial_position(handle,"set-position",
      "set-position",5,&position,&duplicate,&version,error,sizeof(error))==1);
  assert(version==6);
  size_t required=0;
  assert(surge_context_graph_observe_focused_cognition(handle,32,64,nullptr,0,
      &required,error,sizeof(error))==0);
  assert(required>1);
  std::vector<char> output(required);
  assert(surge_context_graph_observe_focused_cognition(handle,32,64,output.data(),
      output.size(),&required,error,sizeof(error))==1);
  const std::string json(output.data());
  assert(json.find("\"focus_region\"")!=std::string::npos);
  assert(json.find("\"center\":[1,0,0]")!=std::string::npos);
  assert(json.find("\"half_extents\":[1,1,1]")!=std::string::npos);
  assert(json.find("\"logical_region_cluster_ids\":[\"focus-cluster\"]")!=std::string::npos);
  assert(json.find("\"processed_region_cluster_ids\":[\"focus-cluster\"]")!=std::string::npos);
  assert(json.find("\"depth\":3")!=std::string::npos);
  assert(json.find("\"cluster_spatial_positions\"")!=std::string::npos);
  assert(json.find("\"position\":[0.5,0,0]")!=std::string::npos);
  assert(json.find("\"budget_exhausted\":false")!=std::string::npos);
  surge_packet_core_close(handle);
  std::filesystem::remove(path,ec);
  return 0;
}
