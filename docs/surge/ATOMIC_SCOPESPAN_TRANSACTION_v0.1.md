# Atomic ScopeSpan Semantic Transaction v0.1

## Contract

The atomic RecognitionPacket + SemanticDelta API now accepts ScopeSpan records.
A transaction may include graph nodes, relations, ScopeSpan proposals and focus
updates. They are staged and validated together before the packet coordinator
is allowed to append any event.

The atomic path performs:
1. Validate the RecognitionPacket and immutable NFC UTF-8 source snapshot.
2. Decode all graph nodes and relations into typed Core records.
3. Decode every ScopeSpan and its opener/optional closer anchors.
4. Require each ScopeSpan packet_refs to include the RecognitionPacket being
   admitted in this transaction.
5. Validate all node/relation/scope/focus operations against one staged graph
   state. Scope anchors are checked against the same exact source snapshot and
   SHA-256 digest used by the packet.
6. Encode the prepared SemanticDelta events, including DELTA_META and
   SCOPE_SPAN_ADD records.
7. Commit SEMANTIC_PACKET_ADMITTED plus all semantic events in one
   EventJournal transaction.
8. Replay the committed transaction to publish ContextGraph state.

Any malformed ScopeSpan, invalid anchor, missing target, invalid relation or
state-version conflict is rejected before PacketAdmissionCoordinator commits.
The transaction therefore cannot leave an admitted packet without its scoped
semantic graph projection.

## Interfaces

- C ABI: surge_packet_core_admit_and_apply now accepts
  SurgeScopeSpanInput[] and scope_count alongside nodes and relations.
- Python: SemanticCoreBridge.admit_and_apply converts typed ScopeSpan records
  into the same atomic C ABI call.
- RecognitionPacketAdapter.admit_and_apply_to_core remains the packet schema,
  digest and range validation boundary.
- Existing standalone surge_context_graph_apply_scoped remains available for
  graph-only semantic updates; it is not the packet admission path.

## Provenance rule

Each atomic ScopeSpan must list the current packet ID in packet_refs. Its
opener and optional closer anchors must identify a packet listed in that
provenance set. This prevents an atomic semantic delta from persisting a span
that is disconnected from the packet being committed.

## Conformance

Python integration tests cover:
- valid ScopeSpan + packet + nodes committed with one state-version increment;
- ScopeSpan restored after reopening the same EventJournal;
- invalid exact anchor rejected with packet_count, scope_count and journal
  state_version unchanged.

The C++ staged validation remains the authoritative validator. Python does not
decide whether a scope is closed, unresolved or safe to emit as an
unconditional claim.
