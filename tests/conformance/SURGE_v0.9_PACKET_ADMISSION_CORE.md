# Packet Admission Core Conformance v0.1

| ID | Scenario | Expected |
|---|---|---|
| PAC-01 | First trusted packet | One journal commit and atomic in-memory publication |
| PAC-02 | Exact retry | Duplicate success, no new event/version |
| PAC-03 | Missing source snapshot validation flag | SOURCE_VALIDATION_REQUIRED |
| PAC-04 | Missing range validation flag | SOURCE_VALIDATION_REQUIRED |
| PAC-05 | Missing required identity or invalid offset basis | PACKET_CORE_INVARIANT_FAILED |
| PAC-06 | Same packet ID with changed canonical digest | PACKET_ID_REUSED |
| PAC-07 | Reused source sequence for another packet | SOURCE_SEQUENCE_REUSED |
| PAC-08 | Changed content for an existing source range | RANGE_CONTENT_CONFLICT |
| PAC-09 | Sequence gap | Packet committed, watermark remains before gap |
| PAC-10 | Restart and replay | Packet count, source watermark and idempotency reconstructed |
| PAC-11 | Retry after restart | Duplicate success, no new journal event |
| PAC-12 | Fill missing sequence after restart | Watermark advances across previously committed later packet |

This suite validates the typed C++ transaction coordinator. It does not prove
that a hostile caller cannot forge adapter-validation flags, nor does it prove
OS power-loss durability or cross-process writer safety.
