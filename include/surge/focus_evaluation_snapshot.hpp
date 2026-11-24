#pragma once

#include "surge/context_graph.hpp"
#include "surge/snapshot.hpp"
#include "surge/snapshot_domains.hpp"
#include <algorithm>
#include <iterator>
#include <cstdint>
#include <vector>
#include <set>
#include <string>

namespace surge {

struct FocusEvaluationSnapshotMetadata {
  std::uint64_t evaluated_state_version{0};
  std::string focus_digest;
  // Digest of the graph state independent of the selected Focus region.
  std::string graph_digest;
  std::string observation_digest;
};

// Validates that an observation is safe to promote to a completed-evaluation
// snapshot. This remains a pure contract gate; persistence and Diff operate
// on the validated SnapshotDocument produced below.
inline bool validate_completed_focus_evaluation(
    const CognitionObservation& observation,
    const FocusEvaluationSnapshotMetadata& metadata,
    std::string& error) {
  error.clear();
  if (!observation.error.empty()) {
    error = "FOCUS_EVALUATION_INVALIDATED";
    return false;
  }
  if (!observation.complete) {
    error = "FOCUS_EVALUATION_INCOMPLETE";
    return false;
  }
  if (metadata.focus_digest.empty() || metadata.graph_digest.empty() ||
      metadata.observation_digest.empty()) {
    error = "FOCUS_EVALUATION_PROVENANCE_MISSING";
    return false;
  }
  if (metadata.evaluated_state_version != observation.state_version) {
    error = "FOCUS_EVALUATION_STATE_VERSION_MISMATCH";
    return false;
  }
  const std::set<std::string> logical(
      observation.logical_region_cluster_ids.begin(),
      observation.logical_region_cluster_ids.end());
  const std::set<std::string> processed(
      observation.processed_region_cluster_ids.begin(),
      observation.processed_region_cluster_ids.end());
  const std::set<std::string> deferred(
      observation.deferred_region_cluster_ids.begin(),
      observation.deferred_region_cluster_ids.end());
  if (logical.size() != observation.logical_region_cluster_ids.size() ||
      processed.size() != observation.processed_region_cluster_ids.size() ||
      deferred.size() != observation.deferred_region_cluster_ids.size()) {
    error = "FOCUS_EVALUATION_DUPLICATE_ROOT";
    return false;
  }
  if (!deferred.empty()) {
    error = "FOCUS_EVALUATION_DEFERRED_ROOTS";
    return false;
  }
  if (logical != processed) {
    error = "FOCUS_EVALUATION_ROOT_SET_MISMATCH";
    return false;
  }
  std::set<std::string> node_ids, relation_ids;
  for (const auto& node : observation.nodes) {
    if (node.id.empty() || !node_ids.insert(node.id).second) {
      error = "FOCUS_EVALUATION_DUPLICATE_OR_INVALID_NODE";
      return false;
    }
  }
  for (const auto& root_id : logical) {
    if (!node_ids.count(root_id)) {
      error = "FOCUS_EVALUATION_ROOT_NODE_MISSING";
      return false;
    }
  }
  for (const auto& relation : observation.relations) {
    if (relation.id.empty() || !relation_ids.insert(relation.id).second) {
      error = "FOCUS_EVALUATION_DUPLICATE_OR_INVALID_RELATION";
      return false;
    }
    if (!node_ids.count(relation.from) || !node_ids.count(relation.to)) {
      error = "FOCUS_EVALUATION_RELATION_ENDPOINT_MISSING";
      return false;
    }
  }
  return true;
}


struct FocusEvaluationSnapshotDiff {
  // Focus changes are independent from graph-content changes. A changed
  // Focus digest does not suppress the graph ID comparison.
  bool focus_changed{false};
  bool graph_changed{false};
  std::vector<std::string> added_roots, removed_roots;
  std::vector<std::string> added_node_ids, removed_node_ids;
  std::vector<std::string> added_relation_ids, removed_relation_ids;
};

namespace focus_snapshot_detail {
inline void append_u32(std::string& out, std::uint32_t value) {
  for (int shift=24; shift>=0; shift-=8)
    out.push_back(static_cast<char>((value >> shift) & 0xff));
}
inline void append_string(std::string& out, const std::string& value) {
  append_u32(out, static_cast<std::uint32_t>(value.size()));
  out.append(value);
}
inline void append_set(std::string& out, std::vector<std::string> values) {
  std::sort(values.begin(), values.end());
  append_u32(out, static_cast<std::uint32_t>(values.size()));
  for (const auto& value : values) append_string(out, value);
}
inline bool read_u32(const std::string& in, std::size_t& pos, std::uint32_t& value) {
  if (pos > in.size() || in.size()-pos < 4) return false;
  value=0;
  for(int i=0;i<4;++i) value=(value<<8)|static_cast<unsigned char>(in[pos++]);
  return true;
}
inline bool read_string(const std::string& in, std::size_t& pos, std::string& value) {
  std::uint32_t size=0;
  if(!read_u32(in,pos,size) || size>in.size()-pos) return false;
  value.assign(in.data()+pos,size); pos+=size; return true;
}
inline bool read_set(const std::string& in, std::size_t& pos,
                     std::vector<std::string>& values) {
  std::uint32_t count=0;
  if(!read_u32(in,pos,count) || count>1000000U) return false;
  values.clear(); values.reserve(count);
  for(std::uint32_t i=0;i<count;++i) {
    std::string value;
    if(!read_string(in,pos,value)) return false;
    values.push_back(std::move(value));
  }
  return std::is_sorted(values.begin(),values.end()) &&
      std::adjacent_find(values.begin(),values.end())==values.end();
}
inline bool decode_manifest(const SnapshotDocument& doc, std::string& focus,
    std::string& graph, std::string& observation, std::vector<std::string>& roots,
    std::vector<std::string>& nodes, std::vector<std::string>& relations) {
  if(doc.schema_id!="surge.focus-evaluation.v1" || doc.payload.size()<4 ||
     doc.payload.compare(0,4,"FES1")!=0) return false;
  std::size_t pos=4;
  return read_string(doc.payload,pos,focus) &&
      read_string(doc.payload,pos,graph) &&
      read_string(doc.payload,pos,observation) &&
      read_set(doc.payload,pos,roots) && read_set(doc.payload,pos,nodes) &&
      read_set(doc.payload,pos,relations) && pos==doc.payload.size();
}
}  // namespace focus_snapshot_detail

// Produces a compact evaluation manifest. Full graph state remains owned by
// ContextGraphSnapshotDomain, so this reuses SnapshotCodec/Store without a
// parallel graph persistence mechanism.
inline bool make_completed_focus_snapshot_document(
    const CognitionObservation& observation,
    const FocusEvaluationSnapshotMetadata& metadata,
    const std::string& context_id, std::uint64_t journal_sequence,
    SnapshotDocument* output, std::string& error) {
  error.clear();
  if(!output || context_id.empty()) {
    error="FOCUS_SNAPSHOT_ARGUMENT_REQUIRED"; return false;
  }
  if(!validate_completed_focus_evaluation(observation,metadata,error)) return false;
  SnapshotDocument candidate;
  candidate.schema_id="surge.focus-evaluation.v1";
  candidate.context_id=context_id;
  candidate.state_version=observation.state_version;
  candidate.journal_sequence=journal_sequence;
  candidate.payload="FES1";
  focus_snapshot_detail::append_string(candidate.payload,metadata.focus_digest);
  focus_snapshot_detail::append_string(candidate.payload,metadata.graph_digest);
  focus_snapshot_detail::append_string(candidate.payload,metadata.observation_digest);
  focus_snapshot_detail::append_set(candidate.payload,observation.logical_region_cluster_ids);
  std::vector<std::string> nodes,relations;
  for(const auto& node:observation.nodes) nodes.push_back(node.id);
  for(const auto& relation:observation.relations) relations.push_back(relation.id);
  focus_snapshot_detail::append_set(candidate.payload,std::move(nodes));
  focus_snapshot_detail::append_set(candidate.payload,std::move(relations));
  *output=std::move(candidate); return true;
}

// Captures a Focus manifest using the canonical full-graph digest. The
// state version is sampled on both sides of capture to reject concurrent writes.
inline bool make_completed_focus_snapshot_document(
    ContextGraph& graph, const CognitionObservation& observation,
    const std::string& focus_digest, const std::string& observation_digest,
    const std::string& context_id, std::uint64_t journal_sequence,
    SnapshotDocument* output, std::string& error) {
  error.clear();
  const auto before_version = graph.state_version();
  if (observation.state_version != before_version) {
    error = "FOCUS_SNAPSHOT_GRAPH_STATE_VERSION_MISMATCH";
    return false;
  }
  ContextGraphSnapshotDomain domain(graph);
  std::string graph_digest;
  if (!domain.canonical_digest(&graph_digest, &error)) return false;
  const auto after_version = graph.state_version();
  if (before_version != after_version) {
    error = "FOCUS_SNAPSHOT_GRAPH_CHANGED_DURING_CAPTURE";
    return false;
  }
  FocusEvaluationSnapshotMetadata metadata;
  metadata.evaluated_state_version = observation.state_version;
  metadata.focus_digest = focus_digest;
  metadata.graph_digest = graph_digest;
  metadata.observation_digest = observation_digest;
  return make_completed_focus_snapshot_document(observation, metadata,
      context_id, journal_sequence, output, error);
}

inline bool validate_focus_snapshot_against_graph(
    const SnapshotDocument& manifest, ContextGraph& graph,
    std::string& error) {
  error.clear();
  if (manifest.context_id.empty() || manifest.schema_id !=
      "surge.focus-evaluation.v1") {
    error = "FOCUS_SNAPSHOT_GRAPH_MANIFEST_INVALID";
    return false;
  }
  const auto version_before = graph.state_version();
  if (manifest.state_version != version_before) {
    error = "FOCUS_SNAPSHOT_GRAPH_STATE_VERSION_MISMATCH";
    return false;
  }
  ContextGraphSnapshotDomain domain(graph);
  std::string digest;
  if (!domain.canonical_digest(&digest, &error)) return false;
  const auto version_after = graph.state_version();
  if (version_before != version_after) {
    error = "FOCUS_SNAPSHOT_GRAPH_CHANGED_DURING_VALIDATION";
    return false;
  }
  std::string focus, graph_digest, observation;
  std::vector<std::string> roots, nodes, relations;
  if (!focus_snapshot_detail::decode_manifest(manifest, focus, graph_digest,
      observation, roots, nodes, relations)) {
    error = "FOCUS_SNAPSHOT_DIFF_MANIFEST_INVALID";
    return false;
  }
  if (digest != graph_digest) {
    error = "FOCUS_SNAPSHOT_GRAPH_DIGEST_MISMATCH";
    return false;
  }
  return true;
}

// Append-only in-memory timeline. Every append must be a compatible,
 // monotonic completed snapshot; history never mutates the underlying graph.
class FocusSnapshotHistory {
 public:
  bool append(const SnapshotDocument& snapshot, std::string& error) {
    error.clear();
    std::string focus, graph, observation;
    std::vector<std::string> roots, nodes, relations;
    if (!focus_snapshot_detail::decode_manifest(snapshot, focus, graph,
        observation, roots, nodes, relations)) {
      error = "FOCUS_SNAPSHOT_HISTORY_MANIFEST_INVALID";
      return false;
    }
    if (!snapshots_.empty()) {
      const auto& previous = snapshots_.back();
      if (previous.context_id != snapshot.context_id) {
        error = "FOCUS_SNAPSHOT_HISTORY_CONTEXT_MISMATCH";
        return false;
      }
      if (previous.state_version > snapshot.state_version ||
          previous.journal_sequence > snapshot.journal_sequence) {
        error = "FOCUS_SNAPSHOT_HISTORY_WATERMARK_REGRESSION";
        return false;
      }
    }
    snapshots_.push_back(snapshot);
    return true;
  }
  std::size_t size() const { return snapshots_.size(); }
  const std::vector<SnapshotDocument>& snapshots() const { return snapshots_; }
  bool diff(std::size_t earlier, std::size_t later,
            FocusEvaluationSnapshotDiff* output, std::string& error) const {
    if (earlier >= snapshots_.size() || later >= snapshots_.size() ||
        earlier >= later) {
      error = "FOCUS_SNAPSHOT_HISTORY_RANGE_INVALID";
      return false;
    }
    return diff_completed_focus_snapshots(
        snapshots_[earlier], snapshots_[later], output, error);
  }
 private:
  std::vector<SnapshotDocument> snapshots_;
};

inline bool diff_completed_focus_snapshots(
    const SnapshotDocument& before, const SnapshotDocument& after,
    FocusEvaluationSnapshotDiff* output, std::string& error) {
  error.clear();
  if(!output) { error="FOCUS_SNAPSHOT_DIFF_ARGUMENT_REQUIRED"; return false; }
  if(before.context_id.empty() || before.context_id!=after.context_id) {
    error="FOCUS_SNAPSHOT_DIFF_CONTEXT_MISMATCH"; return false;
  }
  if(before.state_version>after.state_version ||
     before.journal_sequence>after.journal_sequence) {
    error="FOCUS_SNAPSHOT_DIFF_WATERMARK_REGRESSION"; return false;
  }
  std::string bf,af,bg,ag,bo,ao;
  std::vector<std::string> br,ar,bn,an,be,ae;
  if(!focus_snapshot_detail::decode_manifest(before,bf,bg,bo,br,bn,be) ||
     !focus_snapshot_detail::decode_manifest(after,af,ag,ao,ar,an,ae)) {
    error="FOCUS_SNAPSHOT_DIFF_MANIFEST_INVALID"; return false;
  }
  FocusEvaluationSnapshotDiff candidate;
  candidate.focus_changed = bf != af;
  candidate.graph_changed = bg != ag;
  auto diff=[](const std::vector<std::string>& old_values,
               const std::vector<std::string>& new_values,
               std::vector<std::string>& added,
               std::vector<std::string>& removed) {
    std::set_difference(new_values.begin(),new_values.end(),
        old_values.begin(),old_values.end(),std::back_inserter(added));
    std::set_difference(old_values.begin(),old_values.end(),
        new_values.begin(),new_values.end(),std::back_inserter(removed));
  };
  diff(br,ar,candidate.added_roots,candidate.removed_roots);
  diff(bn,an,candidate.added_node_ids,candidate.removed_node_ids);
  diff(be,ae,candidate.added_relation_ids,candidate.removed_relation_ids);
  candidate.graph_changed = candidate.graph_changed ||
      !candidate.added_roots.empty() || !candidate.removed_roots.empty() ||
      !candidate.added_node_ids.empty() || !candidate.removed_node_ids.empty() ||
      !candidate.added_relation_ids.empty() || !candidate.removed_relation_ids.empty();
  *output=std::move(candidate); return true;
}

}  // namespace surge
