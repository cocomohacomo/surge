# Versioned Journal Tail API v0.1

## Watermark meaning

The journal's state version advances once per committed transaction, not once per event. Each in-memory JournalEvent returned by EventJournal is now tagged with the state version of the transaction that committed it. This value is reconstructed from the durable COMMIT frame on recovery and is not serialized as user-controlled event data.

## Bounded tail read

EventJournal::committed_tail(after_state_version, limits) returns every committed event belonging to transactions strictly after the supplied watermark. Events from the same transaction share one state_version. The default read budget is 65,536 events and 16 MiB of combined event ID, kind, and payload bytes.

The operation is read-only and all-or-nothing. It rejects a closed journal, a watermark ahead of the current state, version gaps or regressions, and event/combined event-byte budget exhaustion. On failure, the returned event list is empty. A watermark equal to the current state returns an empty successful tail.

## Restore safety

This API supplies the exact transaction watermark needed by a future snapshot-plus-tail replay coordinator. It does not apply events to domain state, prove cross-domain semantic equivalence, truncate the journal, or authorize compaction. Callers must still validate the snapshot context and schema, replay each transaction atomically, and compare canonical state with full replay before enabling storage compaction.
