# RecognitionPacket Adapter Conformance v0.1

| Case | Input | Expected |
|---|---|---|
| RPA-01 | Schema-valid packet, matching NFC snapshot and range digest | ADMITTED |
| RPA-02 | Exact packet retry with same idempotency key | Return original Admission |
| RPA-03 | Decomposed source normalized to NFC before byte anchoring | Digest matches NFC bytes |
| RPA-04 | Unknown JSON property | SCHEMA_INVALID |
| RPA-05 | Full snapshot digest mismatch | SOURCE_SNAPSHOT_DIGEST_MISMATCH |
| RPA-06 | Range end exceeds snapshot byte length | SOURCE_RANGE_OUT_OF_BOUNDS |
| RPA-07 | Range digest mismatch | SOURCE_RANGE_DIGEST_MISMATCH |
| RPA-08 | Same idempotency key, changed canonical packet | IDEMPOTENCY_KEY_REUSED |
| RPA-09 | Same stream sequence, different packet ID | SOURCE_SEQUENCE_REUSED |
| RPA-10 | REVISION without predecessor and revision sequence | REVISION_LINEAGE_REQUIRED |

These tests establish adapter admission behavior only. They do not establish
durable Core integration, semantic inference, multi-process concurrency or
full D01-D20 end-to-end conformance.
