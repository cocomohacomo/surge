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

typedef struct SurgeRetrievalTicketInput {
  const char* ticket_id;
  const char* context_id;
  const char* packet_ref;
  const char* source_range_digest;
  const char* omitted_digest;
  const char* policy_version;
  uint64_t state_version;
  const char* const* omitted_refs;
  size_t omitted_count;
} SurgeRetrievalTicketInput;

typedef struct SurgeClassificationProposalInput {
  const char* proposal_id;
  const char* packet_ref;
  uint64_t base_state_version;
  uint64_t context_hint_state_version;
  const char* packet_source_generation;
  uint64_t packet_source_sequence;
  const char* serialized_json;
} SurgeClassificationProposalInput;

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

// Append a valid-time interval revision for an existing Claim. Empty valid_until means open-ended.
int surge_context_graph_apply_claim_validity(void* handle,
    const char* delta_id, const char* idempotency_key, uint64_t expected_state_version,
    const char* claim_id, const char* valid_from, const char* valid_until,
    const char* basis_evidence_id, uint64_t revision,
    int* duplicate, uint64_t* state_version, char* error, size_t error_capacity);

int surge_question_aspect_set_status(void* handle, const char* aspect_id,
    const char* status, uint64_t expected_state_version, char* error, size_t error_capacity);

uint64_t surge_packet_core_state_version(void* handle);
size_t surge_packet_core_packet_count(void* handle);
size_t surge_context_graph_scope_count(void* handle);

// Evaluate the latest Claim valid-time revision at an RFC3339 instant.
int surge_context_graph_claim_validity_at(void* handle, const char* claim_id,
    const char* instant, char* output, size_t output_capacity,
    char* error, size_t error_capacity);

// Read-only deterministic lexical retrieval over replayed durable graph state.
// kinds_csv is a comma-separated allow-list (e.g. CLUSTER,CLAIM,SCOPE_SPAN); empty means all.
// On insufficient output capacity, required_capacity receives the needed byte count including NUL.
int surge_context_graph_retrieve(void* handle, const char* query, const char* kinds_csv,
    size_t limit, char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);

// Paginated variant of surge_context_graph_retrieve. Offset is applied after deterministic ranking.
// Each page is pinned to the returned state_version; callers must verify it remains stable across pages.
int surge_context_graph_retrieve_page(void* handle, const char* query, const char* kinds_csv,
    size_t offset, size_t limit, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity);

// Read a durable dependency-evaluation record by SemanticDelta ID as JSON.
// On insufficient output capacity, required_capacity receives the needed byte count including NUL.
int surge_context_graph_dependency_evaluation(void* handle, const char* delta_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);

// Durable RetrievalTicket issue/resolve operations share the Core EventJournal.
int surge_retrieval_ticket_issue(void* handle, const SurgeRetrievalTicketInput* ticket,
    char* error, size_t error_capacity);
int surge_retrieval_ticket_resolve(void* handle, const char* ticket_id,
    uint64_t current_state_version, int query_replayable,
    const char* const* reevaluated_refs, size_t reevaluated_count,
    char* output, size_t output_capacity, size_t* required_capacity,
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

// Extended atomic API. The legacy entry point above remains ABI-compatible.
int surge_packet_core_admit_and_apply_with_proposal(
    void* handle, const SurgeRecognitionPacketInput* packet,
    const unsigned char* snapshot, size_t snapshot_size,
    const char* delta_id, const char* semantic_idempotency_key,
    uint64_t expected_state_version,
    const SurgeGraphNodeInput* nodes, size_t node_count,
    const SurgeGraphRelationInput* relations, size_t relation_count,
    const SurgeScopeSpanInput* scopes, size_t scope_count,
    const char* focus_cluster_id, const char* focus_interest_id,
    const SurgeClassificationProposalInput* proposal,
    int* duplicate, uint64_t* state_version,
    char* packet_status, size_t status_capacity,
    char* error, size_t error_capacity);

// Retrieve the exact serialized ClassificationProposal from the committed journal.
// On insufficient capacity, required_capacity includes the trailing NUL byte.
int surge_classification_proposal_get(void* handle, const char* proposal_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
