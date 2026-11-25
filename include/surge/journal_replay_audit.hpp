#pragma once

#include "surge/context_graph.hpp"
#include "surge/event_journal.hpp"
#include "surge/packet_admission.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace surge {

// Structural audit of the active Journal tail after Coordinator and Graph
// recovery. The expected digest must be captured from a trusted pre-fault
// projection or recovery manifest; it is not derived from the same Graph.
struct JournalReplayAuditResult {
  bool ok{false};
  std::uint64_t journal_state_version{0};
  std::uint64_t graph_state_version{0};
  std::size_t journal_packet_count{0};
  std::size_t coordinator_packet_count{0};
  std::size_t semantic_packet_count{0};
  std::size_t semantic_event_count{0};
  std::size_t node_count{0};
  std::size_t relation_count{0};
  std::string node_relation_digest;
  std::string error;
};

inline JournalReplayAuditResult audit_journal_replay(
    const EventJournal& journal,
    const PacketAdmissionCoordinator& coordinator,
    const ContextGraph& graph,
    const std::string& expected_node_relation_digest) {
  JournalReplayAuditResult result;
  result.journal_state_version = journal.state_version();
  result.graph_state_version = graph.state_version();
  result.coordinator_packet_count = coordinator.packet_count();
  result.node_count = graph.nodes().size();
  result.relation_count = graph.relations().size();
  result.node_relation_digest = graph.node_relation_digest();

  const auto checkpoint = journal.checkpoint_info();
  const auto events = journal.committed_events();
  std::set<std::string> event_ids;
  std::uint64_t expected_version = checkpoint.base_state_version;
  std::size_t index = 0;

  while (index < events.size()) {
    const auto version = events[index].state_version;
    if (version == 0 || expected_version == UINT64_MAX ||
        version != expected_version + 1) {
      result.error = "AUDIT_JOURNAL_STATE_VERSION_GAP";
      return result;
    }
    expected_version = version;

    const auto begin = index;
    while (index < events.size() && events[index].state_version == version) {
      const auto& event = events[index];
      if (event.event_id.empty() || !event_ids.insert(event.event_id).second) {
        result.error = "AUDIT_EVENT_ID_EMPTY_OR_DUPLICATE";
        return result;
      }
      if (event.kind == "RECOGNITION_PACKET_ADMITTED" ||
          event.kind == "SEMANTIC_PACKET_ADMITTED") {
        ++result.journal_packet_count;
        if (event.kind == "SEMANTIC_PACKET_ADMITTED")
          ++result.semantic_packet_count;
      }
      ++index;
    }

    std::size_t packet_events = 0;
    std::size_t packet_index = index;
    for (std::size_t i = begin; i < index; ++i) {
      if (events[i].kind == "RECOGNITION_PACKET_ADMITTED" ||
          events[i].kind == "SEMANTIC_PACKET_ADMITTED") {
        ++packet_events;
        packet_index = i;
      }
    }
    if (packet_events > 1) {
      result.error = "AUDIT_MULTIPLE_PACKET_ADMISSIONS_IN_TRANSACTION";
      return result;
    }

    bool has_delta_meta = false;
    std::size_t delta_meta_index = index;
    for (std::size_t i = begin; i < index; ++i) {
      if (events[i].kind == "DELTA_META") {
        has_delta_meta = true;
        delta_meta_index = i;
        ++result.semantic_event_count;
      } else if (has_delta_meta) {
        ++result.semantic_event_count;
      }
    }

    if (packet_events == 1 &&
        events[packet_index].kind == "SEMANTIC_PACKET_ADMITTED") {
      if (!has_delta_meta || delta_meta_index != packet_index + 1 ||
          delta_meta_index + 1 >= index) {
        result.error = "AUDIT_SEMANTIC_PACKET_EVENT_SEQUENCE_INCOMPLETE";
        return result;
      }
    } else if (packet_events == 1 &&
               events[packet_index].kind == "RECOGNITION_PACKET_ADMITTED" &&
               has_delta_meta) {
      result.error = "AUDIT_NON_SEMANTIC_PACKET_HAS_SEMANTIC_EVENTS";
      return result;
    }
  }

  if (expected_version != result.journal_state_version) {
    result.error = "AUDIT_JOURNAL_TAIL_VERSION_MISMATCH";
    return result;
  }
  if (result.journal_packet_count != result.coordinator_packet_count) {
    result.error = "AUDIT_COORDINATOR_PACKET_COUNT_MISMATCH";
    return result;
  }
  if (result.graph_state_version != result.journal_state_version) {
    result.error = "AUDIT_GRAPH_STATE_VERSION_MISMATCH";
    return result;
  }
  if (expected_node_relation_digest.empty() ||
      result.node_relation_digest != expected_node_relation_digest) {
    result.error = "AUDIT_NODE_RELATION_DIGEST_MISMATCH";
    return result;
  }

  result.ok = true;
  result.error.clear();
  return result;
}

}  // namespace surge
