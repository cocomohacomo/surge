# Evidence Provenance Timestamps v0.1

## Purpose

Evidence provenance clocks are distinct from both modeled-world valid time and
Core transaction time. They describe the source and observation lifecycle,
not whether a Claim or Evidence is true at a particular instant.

## Fields

Each append-only revision may contain:

- `observed_at`: when the represented event/fact was observed. This is not
  the time the adapter processed the text.
- `source_published_at`: when the cited source published the material.
- `source_retrieved_at`: when the ingestion system retrieved the source.
- `recorded_state_version`: the Core transaction version that stored this
  provenance revision.

All three timestamps are optional RFC3339 values with an explicit timezone.
An absent field is UNKNOWN. Core never infers a timestamp from prose, file
metadata, HTTP headers, or another timestamp. The fields are independent;
Core does not assume an ordering between observation, publication and
retrieval because backfills, delayed publication and retrospective reporting
are legitimate.

These fields do not replace EvidenceValidity `valid_from`/`valid_until`,
ClaimValidity, source sequence, or transaction time.

## Revision and recovery

Revisions are contiguous per Evidence, append-only, and carry their own
recorded state version. Corrections append a new revision; they never mutate
the previous record. Invalid RFC3339 values, timezone-free values, impossible
calendar dates and non-contiguous revisions are rejected. Replay restores the
complete history and rejects malformed or out-of-order records.

## Current implementation boundary

The v0.1 ledger is an explicit Core persistence primitive. The caller must
supply an existing Evidence identifier; adapters cannot write directly.
Binding this ledger into the main SemanticDelta graph transaction and exposing
it through the public C/Python APIs is a subsequent integration task. Until
that integration lands, this primitive must not be described as automatic
metadata capture during packet ingestion.


## Atomic packet integration v0.2

The packet adapter can now pass an explicit `evidence_provenance` object
alongside the RecognitionPacket, SemanticDelta and ClassificationProposal.
The C++ Core verifies that the target identifier resolves to an EVIDENCE node,
validates every supplied timestamp, prepares the provenance event without
committing it independently, and appends it to the same EventJournal
transaction. The transaction therefore advances State Version once.

The C API and Python bridge expose a read-only history query. Each revision
includes its recorded State Version and preserves unknown fields as JSON null.
The LLM runtime accepts provenance clocks only when the caller supplies them;
it never derives observation/publication/retrieval times from the text or
runtime wall clock. Omitting the argument means no provenance event is added.

This integration supports append-only revisions against existing Evidence
nodes as well as first-time Evidence creation. A provenance event is rejected
if its Evidence target is absent or has another node kind.
