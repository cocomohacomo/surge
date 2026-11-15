#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Opaque, single-process Core handle. The handle owns its journal and coordinator.
void* surge_packet_core_open(const char* journal_path, char* error, size_t error_capacity);
void surge_packet_core_close(void* handle);

// Returns 1 when admitted (including an exact duplicate), otherwise 0.
// Snapshot is an exact byte buffer, not a NUL-terminated string.
int surge_packet_core_admit(
    void* handle,
    const char* packet_id, const char* idempotency_key,
    const char* context_id, const char* source_id,
    const char* source_generation, const char* block_ref,
    const char* offset_basis, uint64_t start_offset, uint64_t end_offset,
    uint64_t source_sequence, const char* source_snapshot_digest,
    const char* source_range_digest, const char* canonical_request_digest,
    const char* revision_of_packet_id, int revision, uint64_t revision_sequence,
    const unsigned char* snapshot, size_t snapshot_size,
    int* duplicate, uint64_t* state_version, uint64_t* watermark,
    char* packet_status, size_t status_capacity,
    char* error, size_t error_capacity);

uint64_t surge_packet_core_state_version(void* handle);
size_t surge_packet_core_packet_count(void* handle);

#ifdef __cplusplus
}
#endif
