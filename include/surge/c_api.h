#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct SurgeRecognitionPacketInput {
  const char* packet_id;
  const char* idempotency_key;
  const char* context_id;
  const char* source_id;
  const char* source_generation;
  const char* block_ref;
  const char* offset_basis;
  uint64_t start_offset;
  uint64_t end_offset;
  uint64_t source_sequence;
  const char* source_snapshot_digest;
  const char* source_range_digest;
  const char* canonical_request_digest;
  const char* revision_of_packet_id;
  int revision;
  uint64_t revision_sequence;
} SurgeRecognitionPacketInput;

typedef struct SurgeGraphNodeInput {
  const char* id;
  const char* kind;       // CLUSTER, ENTITY, EVIDENCE, CLAIM, QUESTION, QUESTION_ASPECT, DEPENDENCY, INTEREST
  const char* content;
  const char* const* references;
  size_t reference_count;
} SurgeGraphNodeInput;

typedef struct SurgeGraphRelationInput {
  const char* id;
  const char* kind;
  const char* from_id;
  const char* to_id;
} SurgeGraphRelationInput;

typedef struct SurgeScopeAnchorInput {
  const char* anchor_id;
  const char* source_digest;
  const char* offset_basis;
  uint64_t start_offset;
  uint64_t end_offset;
  const char* exact_text;
  const char* packet_ref;
} SurgeScopeAnchorInput;

typedef struct SurgeScopeSpanInput {
  const char* span_id;
  const char* kind;
  const char* source_digest;
  const char* offset_basis;
  SurgeScopeAnchorInput opener;
  int has_closer;
  SurgeScopeAnchorInput closer;
  const char* const* target_refs;
  size_t target_count;
  const char* const* antecedent_refs;
  size_t antecedent_count;
  const char* attribution_ref;
  const char* modality_kind;
  const char* temporal_role;
  const char* parent_ref;
  const char* const* packet_refs;
  size_t packet_count;
} SurgeScopeSpanInput;

void* surge_packet_core_open(const char* journal_path, char* error, size_t error_capacity);
void surge_packet_core_close(void* handle);
int surge_packet_core_admit(
    void* handle, const char* packet_id, const char* idempotency_key,
    const char* context_id, const char* source_id, const char* source_generation,
    const char* block_ref, const char* offset_basis, uint64_t start_offset,
    uint64_t end_offset, uint64_t source_sequence, const char* source_snapshot_digest,
    const char* source_range_digest, const char* canonical_request_digest,
    const char* revision_of_packet_id, int revision, uint64_t revision_sequence,
    const unsigned char* snapshot, size_t snapshot_size, int* duplicate,
    uint64_t* state_version, uint64_t* watermark, char* packet_status,
    size_t status_capacity, char* error, size_t error_capacity);

// Atomically validates and commits a typed SemanticDelta to the shared ContextGraph/EventJournal.
int surge_context_graph_apply(
    void* handle, const char* delta_id, const char* idempotency_key,
    uint64_t expected_state_version,
    const SurgeGraphNodeInput* nodes, size_t node_count,
    const SurgeGraphRelationInput* relations, size_t relation_count,
    const char* focus_cluster_id, const char* focus_interest_id,
    int* duplicate, uint64_t* state_version, char* error, size_t error_capacity);

uint64_t surge_packet_core_state_version(void* handle);
size_t surge_packet_core_packet_count(void* handle);
size_t surge_context_graph_scope_count(void* handle);

// Read-only deterministic lexical retrieval over replayed durable graph state.
// kinds_csv is a comma-separated allow-list (e.g. CLUSTER,CLAIM,SCOPE_SPAN); empty means all.
// On insufficient output capacity, required_capacity receives the needed byte count including NUL.
int surge_context_graph_retrieve(void* handle, const char* query, const char* kinds_csv,
    size_t limit, char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);

// One durable transaction for RecognitionPacket admission and SemanticDelta.
int surge_packet_core_admit_and_apply(
    void* handle, const SurgeRecognitionPacketInput* packet,
    const unsigned char* snapshot, size_t snapshot_size,
    const char* delta_id, const char* semantic_idempotency_key,
    uint64_t expected_state_version,
    const SurgeGraphNodeInput* nodes, size_t node_count,
    const SurgeGraphRelationInput* relations, size_t relation_count,
    const SurgeScopeSpanInput* scopes, size_t scope_count,
    const char* focus_cluster_id, const char* focus_interest_id,
    int* duplicate, uint64_t* state_version,
    char* packet_status, size_t status_capacity,
    char* error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
