#include "surge/context_graph.hpp"
#include "surge/packet_admission.hpp"
#include "surge/sha256.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace surge;

static ValidatedRecognitionPacket packet_for(const std::string& source) {
  ValidatedRecognitionPacket p;
  p.packet_id="packet-atomic-1";
  p.idempotency_key="packet-idem-atomic-1";
  p.context_id="context-atomic";
  p.source_id="source-atomic";
  p.source_generation="generation-1";
  p.block_ref="block-1";
  p.offset_basis="UTF8_BYTE_NFC";
  p.start_offset=0;
  p.end_offset=source.size();
  p.source_sequence=1;
  p.source_snapshot_digest=sha256_hex(source);
  p.source_range_digest=sha256_hex(source);
  p.canonical_request_digest=sha256_hex("canonical-packet");
  return p;
}

static SemanticDelta delta_for() {
  SemanticDelta d;
  d.delta_id="semantic-atomic-1";
  d.idempotency_key="semantic-idem-atomic-1";
  d.expected_state_version=0;
  d.nodes={{{"cluster-atomic",NodeKind::Cluster,"budget",{} }},
           {{"evidence-atomic",NodeKind::Evidence,"budget approved",{} }}};
  return d;
}

int main() {
  const std::string source="budget";
  const auto root=std::filesystem::temp_directory_path()/"surge-atomic-semantic-tests";
  std::filesystem::remove_all(root);
  const auto path=(root/"atomic.sgj").string();
  {
    EventJournal journal(path);
    assert(journal.open());
    PacketAdmissionCoordinator packets(journal);
    ContextGraph graph(journal);
    assert(packets.recover());
    assert(graph.open());
    auto delta=delta_for();
    std::vector<JournalEvent> events;
    std::string error;
    assert(graph.prepare_events(delta,events,error));
    assert(!events.empty());
    const auto admitted=packets.admit(packet_for(source),source,events);
    assert(admitted.admitted&&!admitted.duplicate);
    assert(admitted.state_version==1);
    assert(graph.open());
    assert(graph.state_version()==1);
    assert(packets.packet_count()==1);
    assert(graph.nodes().size()==2);
    // Exact retry must not create another packet or semantic transaction.
    std::vector<JournalEvent> retry_events;
    assert(graph.prepare_events(delta,retry_events,error));
    assert(retry_events.empty());
    const auto retry=packets.admit(packet_for(source),source,retry_events);
    assert(retry.admitted&&retry.duplicate);
    assert(journal.state_version()==1);
  }
  {
    EventJournal journal(path);
    assert(journal.open());
    PacketAdmissionCoordinator packets(journal);
    ContextGraph graph(journal);
    assert(packets.recover());
    assert(graph.open());
    assert(packets.packet_count()==1);
    assert(graph.state_version()==1);
    assert(graph.nodes().count("cluster-atomic")==1);
    assert(graph.nodes().count("evidence-atomic")==1);
  }
  {
    const auto fail_path=(root/"reject.sgj").string();
    EventJournal journal(fail_path);
    assert(journal.open());
    PacketAdmissionCoordinator packets(journal);
    ContextGraph graph(journal);
    assert(packets.recover());
    assert(graph.open());
    auto delta=delta_for();
    delta.nodes[0].node.content="";
    std::vector<JournalEvent> events;
    std::string error;
    assert(!graph.prepare_events(delta,events,error));
    assert(error=="NODE_ID_OR_CONTENT_INVALID");
    // The orchestrator must not call packet admission after semantic validation fails.
    assert(journal.state_version()==0);
    assert(packets.packet_count()==0);
  }
  std::filesystem::remove_all(root);
  std::cout<<"SURGE atomic packet-semantic transaction: commit/retry/replay passed\n";
}
