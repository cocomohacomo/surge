#pragma once
#include <stddef.h>
#include <stdint.h>

// C ABI version is independent from the SURGE package version.
// Major changes may break ABI; minor changes are additive; patch changes
// preserve ABI. Major zero denotes the pre-stability compatibility period.
#define SURGE_ABI_VERSION_MAJOR 0u
#define SURGE_ABI_VERSION_MINOR 5u
#define SURGE_ABI_VERSION_PATCH 0u
#define SURGE_ABI_VERSION_ENCODE(major, minor, patch) ((((uint32_t)(major) & 0xffu) << 24) | (((uint32_t)(minor) & 0xffu) << 16) | ((uint32_t)(patch) & 0xffffu))
#ifdef __cplusplus
extern "C" {

// Returns SURGE_ABI_VERSION_ENCODE(major, minor, patch).
uint32_t surge_abi_version(void);
#endif

typedef struct SurgeFocusRegionInput {
  double center_x, center_y, center_z;
  double half_extent_x, half_extent_y, half_extent_z;
} SurgeFocusRegionInput;
typedef struct SurgeFocusRegionCenterInput { double x, y, z; } SurgeFocusRegionCenterInput;
typedef struct SurgeFocusRegionExtentInput { double x, y, z; } SurgeFocusRegionExtentInput;
typedef struct SurgeClusterSpatialPositionInput {
  const char* cluster_id;
  double x, y, z;
} SurgeClusterSpatialPositionInput;
typedef struct SurgeClusterFocusDepthInput {
  const char* cluster_id;
  size_t depth;
} SurgeClusterFocusDepthInput;

typedef struct SurgeChaosProvenanceInput {
  const char* producer_id;
  const char* producer_version;
  const char* policy_version;
  const char* recorded_at;
} SurgeChaosProvenanceInput;
typedef struct SurgeChaosMemberInput {
  const char* member_ref;
  const char* event_ref;
  const char* admission_reason;
  const char* admitted_at;
  const char* source_digest;
} SurgeChaosMemberInput;
typedef struct SurgeChaosAssociationInput {
  const char* association_id;
  const char* from_member_ref;
  const char* to_member_ref;
  const char* association_type;
} SurgeChaosAssociationInput;
typedef struct SurgeChaosAssociationTemporalInput {
  const char* association_id;
  const char* from_member_ref;
  const char* to_member_ref;
  const char* association_type;
  const char* valid_from_utc;
  const char* valid_until_utc;
} SurgeChaosAssociationTemporalInput;
typedef struct SurgeChaosHypothesisReviewInput {
  const char* status;
  const char* epistemic_state;
  const char* const* excluded_member_refs;
  const char* const* exclusion_reasons;
  size_t exclusion_count;
} SurgeChaosHypothesisReviewInput;

typedef struct SurgeChaosScoreObservationInput {
  double value;
  const char* scorer_id;
  const char* scorer_version;
  const char* policy_version;
  const char* observed_at;
} SurgeChaosScoreObservationInput;

typedef struct SurgeChaosPatternHypothesisInput {
  const char* hypothesis_id;
  const char* statement;
  const char* epistemic_state;
  const char* status;
  const char* const* member_refs; size_t member_count;
  const char* const* excluded_member_refs; size_t excluded_member_count;
  const char* const* counterexample_refs; size_t counterexample_count;
  const char* const* alternative_hypothesis_refs; size_t alternative_hypothesis_count;
} SurgeChaosPatternHypothesisInput;

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
  const char* kind;       // CLUSTER, CHAOS_CLUSTER, ENTITY, EVIDENCE, CLAIM, QUESTION, QUESTION_ASPECT, DEPENDENCY, INTEREST, CLUSTER_LIFECYCLE
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

typedef struct SurgeProjectionMetadataInput {
  const char* projection_id;
  const char* idempotency_key;
  const char* context_id;
  const char* producer_id;
  const char* producer_version;
  const char* policy_version;
  uint64_t base_state_version;
} SurgeProjectionMetadataInput;

typedef struct SurgeProjectionRelationInput {
  const char* id;
  const char* kind;
  const char* from_id;
  const char* to_id;
  double weight;
} SurgeProjectionRelationInput;

typedef struct SurgeProjectionMembershipInput {
  const char* relation_id;
  const char* member_ref;
  const char* cluster_ref;
  double upstream_weight;
} SurgeProjectionMembershipInput;

typedef struct SurgeProjectionSpatialPlacementInput {
  const char* cluster_id;
  double x, y, z;
} SurgeProjectionSpatialPlacementInput;

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

typedef struct SurgeEvidenceProvenanceInput {
  const char* evidence_id;
  const char* observed_at;
  const char* source_published_at;
  const char* source_retrieved_at;
  uint64_t revision;
} SurgeEvidenceProvenanceInput;

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

typedef struct SurgeJournalEventInput {
  const char* event_id;
  const char* kind;
  const char* payload;
} SurgeJournalEventInput;

// Append one retention metadata event per transaction. The single-event
// contract preserves one State Version increment per event for all reducers.
// Arbitrary domain event kinds and physical payload deletion are forbidden.
int surge_retention_journal_commit(void* handle, const char* transaction_id,
    const char* idempotency_key, uint64_t expected_state_version,
    const SurgeJournalEventInput* events, size_t event_count,
    int* duplicate, uint64_t* state_version, char* error, size_t error_capacity);
int surge_retention_journal_get_events(void* handle, char* output,
    size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);

typedef struct SurgeResourceBudgetSnapshot {
  size_t resident_bytes_used;
  size_t transient_bytes_used;
  size_t retained_events_used;
  size_t retained_clusters_used;
  size_t decision_candidates_used;
  size_t decision_evidence_used;
  size_t resident_bytes_limit;
  size_t transient_bytes_limit;
  size_t retained_events_limit;
  size_t retained_clusters_limit;
  size_t decision_candidates_limit;
  size_t decision_evidence_limit;
  size_t total_byte_used;
  size_t total_byte_limit;
} SurgeResourceBudgetSnapshot;

typedef struct SurgeResourceBudgetLimits {
  size_t resident_bytes;
  size_t transient_bytes;
  size_t retained_events;
  size_t retained_clusters;
  size_t decision_candidates;
  size_t decision_evidence;
} SurgeResourceBudgetLimits;

// Opens the Core with explicit finite logical resource limits. Zero capacity
// means no resources of that kind may be admitted.
void* surge_packet_core_open_with_limits(const char* journal_path,
    const SurgeResourceBudgetLimits* limits, char* error, size_t error_capacity);
void* surge_packet_core_open(const char* journal_path, char* error, size_t error_capacity);
void surge_packet_core_close(void* handle);
int surge_packet_core_resource_snapshot(void* handle,
    SurgeResourceBudgetSnapshot* snapshot, char* error, size_t error_capacity);
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
// Atomically admit one source packet and its transport-neutral projection batch.
int surge_projection_batch_admit_and_apply(
    void* handle, const SurgeRecognitionPacketInput* packet,
    const unsigned char* normalized_source_snapshot, size_t snapshot_size,
    const SurgeProjectionMetadataInput* metadata,
    const char* const* evidence_refs, size_t evidence_count,
    const SurgeGraphNodeInput* nodes, size_t node_count,
    const SurgeProjectionRelationInput* relations, size_t relation_count,
    const SurgeProjectionMembershipInput* memberships, size_t membership_count,
    const SurgeProjectionSpatialPlacementInput* spatial_placements, size_t placement_count,
    int* duplicate, uint64_t* state_version, char* packet_status,
    size_t status_capacity, char* error, size_t error_capacity);

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
// Append a ClaimValidity revision with durable review provenance.
int surge_context_graph_apply_claim_validity_reviewed(void* handle,
    const char* delta_id, const char* idempotency_key, uint64_t expected_state_version,
    const char* claim_id, const char* valid_from, const char* valid_until,
    const char* basis_evidence_id, uint64_t revision,
    const char* review_evidence_id, const char* reviewer_id,
    int* duplicate, uint64_t* state_version, char* error, size_t error_capacity);

int surge_question_aspect_set_status_batch(void* handle, const char* const* aspect_ids,
    const char* const* statuses, size_t count, uint64_t expected_state_version,
    uint64_t* state_version, char* error, size_t error_capacity);
int surge_question_aspect_set_status(void* handle, const char* aspect_id,
    const char* status, uint64_t expected_state_version, char* error, size_t error_capacity);
int surge_question_aspect_get_status(void* handle, const char* aspect_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);
int surge_question_get_aggregate_status(void* handle, const char* question_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);
int surge_question_memory_set_state(void*,const char*,const char*,uint64_t,char*,size_t);
int surge_question_memory_record_reference(void*,const char*,uint64_t,int*,uint64_t*,char*,size_t);
int surge_question_memory_record_reference_with_dependents(void*,const char*,uint64_t,size_t,size_t,int*,size_t*,char*,size_t);
int surge_question_memory_get_state(void*,const char*,char*,size_t,size_t*,char*,size_t);
int surge_question_memory_recommendation(double,double,double,double,double,double,char*,size_t,size_t*,char*,size_t);
int surge_question_memory_plan(void*,size_t,char*,size_t,size_t*,char*,size_t);
int surge_question_memory_enforce_capacity(void*,const char* const*,const double*,size_t,size_t,uint64_t,char*,size_t,size_t*,char*,size_t);
// Read-only bounded discovery of Questions that depend (directly or transitively)
// on the supplied prerequisite Question. max_depth is 1..64, max_nodes is 1..4096.
int surge_question_get_context_bundle(void* handle, const char* question_id, size_t max_depth, size_t max_nodes, char* output, size_t output_capacity, size_t* required_capacity, char* error, size_t error_capacity);
int surge_question_get_reevaluation_plan(void* handle, const char* question_id, size_t max_depth, size_t max_nodes, char* output, size_t output_capacity, size_t* required_capacity, char* error, size_t error_capacity);
int surge_question_get_reevaluation_candidates(void* handle, const char* question_id,
    size_t max_depth, size_t max_nodes, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity);
int surge_question_route_contradiction(void* handle, const char* relation_id,
    const char* policy_version, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity);
int surge_question_route_contradiction_scoped(void* handle, const char* relation_id,
    const char* policy_version, const char* scope_key, char* output,
    size_t output_capacity, size_t* required_capacity, char* error, size_t error_capacity);

// Durable, proposal-only cross-packet temporal boundary ledger.
int surge_temporal_boundary_add(void* handle, const char* transaction_id,
    const char* idempotency_key, const char* event_id, uint64_t expected_state_version,
    const char* claim_id, const char* role, const char* valid_from, const char* valid_until,
    const char* evidence_id, const char* packet_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);
int surge_temporal_boundary_get(void* handle, const char* claim_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);
int surge_temporal_boundary_clear(void* handle, const char* transaction_id,
    const char* idempotency_key, const char* event_id, uint64_t expected_state_version,
    const char* claim_id, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity);
int surge_temporal_boundary_resolve_candidate(void* handle,const char* transaction_id,const char* idempotency_key,const char* event_id,uint64_t expected_state_version,const char* claim_id,const char* role,const char* candidate_id,char* output,size_t output_capacity,size_t* required_capacity,char* error,size_t error_capacity);
size_t surge_temporal_boundary_pending_count(void* handle);
int surge_temporal_question_get(void* handle, const char* claim_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);
size_t surge_temporal_question_count(void* handle);
int surge_context_graph_focus_get(void* handle, char* output, size_t output_capacity,
    size_t* required_capacity, char* error, size_t error_capacity);
int surge_context_graph_focus_set_chaos(void* handle,const char* delta_id,const char* idempotency_key,
    uint64_t expected_state_version,const char* chaos_cluster_id,const char* interest_id,
    int* duplicate,uint64_t* state_version,char* error,size_t error_capacity);

uint64_t surge_packet_core_state_version(void* handle);
size_t surge_packet_core_packet_count(void* handle);
size_t surge_context_graph_scope_count(void* handle);

// Evaluate the latest Claim valid-time revision at an RFC3339 instant.
int surge_context_graph_claim_validity_at(void* handle, const char* claim_id,
    const char* instant, char* output, size_t output_capacity,
    char* error, size_t error_capacity);

// Journal-backed Chaos Cluster C ABI. Membership does not imply shared topic.
int surge_chaos_cluster_create(void*,const char*,const char*,uint64_t,const char*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_admit_member(void*,const char*,const char*,uint64_t,const char*,const SurgeChaosMemberInput*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_propose_association(void*,const char*,const char*,uint64_t,const char*,const SurgeChaosAssociationInput*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_propose_association_temporal(void*,const char*,const char*,uint64_t,const char*,const SurgeChaosAssociationTemporalInput*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_review_association(void*,const char*,const char*,uint64_t,const char*,const char*,const char*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_expire_due_associations(void*,const char*,const char*,uint64_t,const char*,const char*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_merge(void*,const char*,const char*,uint64_t,const char*,const char* const*,size_t,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_add_pattern_hypothesis(void*,const char*,const char*,uint64_t,const char*,const SurgeChaosPatternHypothesisInput*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_review_pattern_hypothesis(void*,const char*,const char*,uint64_t,const char*,const char*,const SurgeChaosHypothesisReviewInput*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_record_observation(void*,const char*,const char*,uint64_t,const char*,const char*,const SurgeChaosScoreObservationInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_record_entity_observation(void*,const char*,const char*,uint64_t,const char*,const char*,const char*,const SurgeChaosScoreObservationInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_revise_membership(void*,const char*,const char*,uint64_t,const char*,const char*,const char*,int,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_transition(void*,const char*,const char*,uint64_t,const char*,const char*,const char*,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_promote_subset(void*,const char*,const char*,uint64_t,const char*,const char* const*,const char* const*,size_t,const SurgeChaosProvenanceInput*,uint64_t*,char*,size_t);
int surge_chaos_cluster_get(void*,const char*,char*,size_t,size_t*,char*,size_t);
int surge_chaos_cluster_get_bounded(void*,const char*,size_t,size_t,size_t,size_t,size_t,char*,size_t,size_t*,char*,size_t);

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

// Read-only detached snapshot of the committed SURGE cognition state as JSON.
// On insufficient output capacity, required_capacity receives bytes including NUL.
// Spatial Focus mutation events. Region geometry, cluster position and cluster depth
// are separate durable events and each commits in its own SemanticDelta.
int surge_context_graph_set_focus_region(void* handle, const char* delta_id,
    const char* idempotency_key, uint64_t expected_state_version,
    const SurgeFocusRegionInput* region, int* duplicate,
    uint64_t* state_version, char* error, size_t error_capacity);
int surge_context_graph_move_focus_region(void* handle, const char* delta_id,
    const char* idempotency_key, uint64_t expected_state_version,
    const SurgeFocusRegionCenterInput* center, int* duplicate,
    uint64_t* state_version, char* error, size_t error_capacity);
int surge_context_graph_resize_focus_region(void* handle, const char* delta_id,
    const char* idempotency_key, uint64_t expected_state_version,
    const SurgeFocusRegionExtentInput* half_extents, int* duplicate,
    uint64_t* state_version, char* error, size_t error_capacity);
int surge_context_graph_set_cluster_spatial_position(void* handle,
    const char* delta_id, const char* idempotency_key,
    uint64_t expected_state_version,
    const SurgeClusterSpatialPositionInput* position, int* duplicate,
    uint64_t* state_version, char* error, size_t error_capacity);
int surge_context_graph_set_cluster_focus_depth(void* handle,
    const char* delta_id, const char* idempotency_key,
    uint64_t expected_state_version,
    const SurgeClusterFocusDepthInput* depth, int* duplicate,
    uint64_t* state_version, char* error, size_t error_capacity);
// Read-only bounded projection of the persisted logical Focus Region.
int surge_context_graph_observe_focused_cognition(void* handle,
    size_t max_nodes, size_t max_relations, char* output,
    size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);

int surge_context_graph_observe_cognition(void* handle, char* output,
    size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);

/* Read-only cognition candidate recall. Query terms are producer supplied.
 * Output JSON includes retrieval-only scores and the memory state version. */
int surge_context_graph_recall_candidates(void* handle, const char* input_member_id,
    const char* const* query_terms, size_t query_term_count, size_t max_candidates,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);
/* Append external candidate evaluation as Evidence in one budgeted transaction. */
int surge_context_graph_record_cognition_feedback(void* handle,
    const char* feedback_id, const char* candidate_cluster_id,
    const char* input_member_id, const char* outcome, const char* producer_id,
    const char* producer_version, const char* policy_version,
    const char* recorded_at, const char* notes, uint64_t expected_state_version,
    const char* feedback_node_id, const char* relation_id,
    uint64_t* state_version, char* error, size_t error_capacity);

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
int surge_classification_proposal_get(void* handle, const char* proposal_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);
// Atomic packet + SemanticDelta + optional proposal + Evidence provenance.
int surge_packet_core_admit_and_apply_with_metadata(
    void* handle, const SurgeRecognitionPacketInput* packet,
    const unsigned char* snapshot, size_t snapshot_size,
    const char* delta_id, const char* semantic_idempotency_key,
    uint64_t expected_state_version,
    const SurgeGraphNodeInput* nodes, size_t node_count,
    const SurgeGraphRelationInput* relations, size_t relation_count,
    const SurgeScopeSpanInput* scopes, size_t scope_count,
    const char* focus_cluster_id, const char* focus_interest_id,
    const SurgeClassificationProposalInput* proposal,
    const SurgeEvidenceProvenanceInput* provenance,
    int* duplicate, uint64_t* state_version,
    char* packet_status, size_t status_capacity,
    char* error, size_t error_capacity);
int surge_evidence_provenance_get(void* handle, const char* evidence_id,
    char* output, size_t output_capacity, size_t* required_capacity,
    char* error, size_t error_capacity);


#ifdef __cplusplus
}
#endif
