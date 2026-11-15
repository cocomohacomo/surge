# SURGE ContextHint and RetrievalTicket v0.1

## Candidate priority

Mandatory classes, in fixed order:

1. Open ScopeSpan
2. Required QuestionAspect
3. Triggered Dependency
4. Required provenance/lineage
5. Active Focus/Scope

Optional classes follow: related Cluster, historical analogue, low-priority candidate.

Within each class sort by source overlap descending, dependency depth ascending, then immutable ref ID. Canonicalize and deduplicate before budgeting. Producer confidence never evicts a mandatory ref.

## Budget behavior

If mandatory refs fit, include them first, then optional refs. Optional truncation is visible in omitted refs but does not make context incomplete and does not require a RetrievalTicket.

If mandatory refs exceed budget, include the deterministic prefix that fits, set context_complete=false and issue one idempotent ticket scoped by Context, packet, source range digest, omitted digest and policy version. The omitted refs remain retrievable.

## RetrievalTicket resolution

- Same state version: return the exact omitted refs.
- Changed state version with replayable query: caller supplies the re-evaluated current refs; return them with the current state version.
- Changed state version without replay capability: return STALE.
- Resolution is read-only; it does not commit graph state.
- Repeated resolution at the same current state version returns the same logical result.

## Limits

This first registry is in-memory and process-local. It uses a non-cryptographic FNV-1a key fingerprint and does not implement Context authorization, expiry, supersession, durable ticket records, query predicate persistence or automatic graph retrieval. Those are required before production use.
