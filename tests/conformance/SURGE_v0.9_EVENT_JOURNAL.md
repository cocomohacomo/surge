# SURGE v0.9 Event Journal Conformance

| ID | Scenario | Expected |
|---|---|---|
| J01 | Commit a transaction with multiple events | One state-version increment; all events visible |
| J02 | Retry identical idempotency request | Duplicate success; no new events/version |
| J03 | Reuse idempotency key with changed request | IDEMPOTENCY_KEY_REUSED |
| J04 | Commit with stale expected version | STATE_VERSION_CONFLICT |
| J05 | Duplicate event ID in batch or journal | Rejected; no partial transaction |
| J06 | Restart after committed transactions | Ordered events and state version recovered |
| J07 | Truncated trailing frame | Prior committed transactions retained; tail ignored |
| J08 | Complete frame with invalid CRC | Recovery fails closed |
| J09 | Begin/event records without commit marker | Transaction invisible after restart |
| J10 | Concurrent in-process commits | Serialized; no duplicate version |
| J11 | Payload includes quotes, pipes and newlines | Exact payload round-trips |
| J12 | Transaction ID reuse | TRANSACTION_ID_REUSED |

These tests cover journal framing and transaction visibility, not domain-level semantic atomicity.
