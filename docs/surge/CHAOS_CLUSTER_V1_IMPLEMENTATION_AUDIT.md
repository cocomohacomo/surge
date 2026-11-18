# Chaos Cluster v1.0 implementation audit

Audit baseline: main `06daf8ac411d861539e9d65f3ee3a9b6090b989c` plus
the Focus retrieval integration merged immediately afterward.
Normative references: `SPECIFICATION_v1.0.md`,
`CHAOS_CLUSTER_MODEL_v1.0.md`, and
`schemas/surge-chaos-cluster-v1.0.schema.json`.

This is a source-level implementation audit, not a security certification or
independent semantic-quality evaluation.

## Status definitions

- **Implemented**: native code, exposed API where applicable, and regression
  coverage exist.
- **Partial**: a meaningful subset exists, but one or more normative contracts
  or public interfaces are missing.
- **Missing**: no corresponding implementation was found in the audited path.
- **Unverified**: source suggests behavior, but required failure/concurrency/
  durability evidence has not been run.

## Findings

| Priority | Requirement | Status | Gap |
|---|---|---|---|
| P0 | Orthogonal score observations | Partial | Cluster, Member Salience, Association Confidence and PatternHypothesis Confidence persist as scorer-bound append-only histories and are exposed through C ABI/Python bounded projections. Focus retrieval now ranks by observed member Salience and combines admission-reason diversity; cluster-level novelty/diversity/ambiguity/utility are exposed as context but not yet used in a configurable multi-factor ranker. |
| P0 | Membership status and unmembership | Partial | Native C++ ledger, CC4 replay, C ABI/Python revision endpoint, bounded JSON history and replay tests are implemented; exhaustive lifecycle/legacy-fixture tests remain. |
| P1 | PatternHypothesis auditability | Partial | Exclusion reasons, append-only review revisions and confidence observations now persist and are exposed in bounded JSON; negative/terminal transition and migration matrix still needs expansion. |
| P1 | Association valid-time | Partial | UTC valid_from/valid_until persist in CC6; temporal C ABI/Python proposal and explicit as-of expiry transaction append EXPIRED provenance revisions. Automatic scheduling is intentionally external to Core. |
| P1 | Retrieval diversity | Partial | Focus retrieval ranks known member Salience before UNKNOWN within admission-reason groups, round-robins groups, excludes inactive memberships, and emits selection rationale. It does not yet provide configurable multi-factor scoring or use all cluster-level dimensions to alter selection. |
| P1 | Retrieval query dimensions | Partial | Typed Chaos retrieval now filters association type/status, valid-time, member admission-time, producer provenance and member refs; hypothesis/counterexample controls and bounded parent/child lineage traversal are implemented. Independent member/association/hypothesis pagination, stable offsets, score-based ranking policies, unknown-score ordering, and page/dataset completeness metadata are now implemented. Association endpoint expansion and exhaustive retrieval-budget combinations still need conformance coverage. |
| P1 | Lifecycle and operations | Partial | ARCHIVED -> DORMANT reactivation is an explicit provenance-bound transition; atomic merge, fork/split/subset promotion, and CC8 lifecycle reason/provenance history are implemented. Expanded conflict, migration and lifecycle conformance remains. |
| P1 | CC01-CC20 executable conformance | Partial | C++/C ABI/Python tests cover persistence, replay, lifecycle, association, hypotheses, promotion/fork/split, Focus and retrieval. Dedicated C++ regressions now explicitly cover CC02, CC04, CC06, CC10, CC12 and CC19; retention tests cover CC05/CC20 planner invariants. CC14/CC15 now have a shared-EventJournal end-to-end regression covering non-promotion, QuestionAspect resolution isolation, and independent restart replay; the full 20-case restart/negative matrix remains open. |
| P2 | Memory and forgetting | Partial | A deterministic, digest-verified retention manifest planner now exposes salience/utility/interest/dependency/recency factors, archives without deleting, and emits explicit manifest-bound tombstone proposals. Core journal integration, payload compaction, restore drill and physical deletion verification remain open. |
| P2 | Operational safety | Partial | EventJournal acquires an OS-level exclusive writer lock (POSIX `flock`, Windows exclusive `CreateFile` sharing mode), serializes same-handle operations with a mutex, and synchronizes appended frames (`fsync` / `FlushFileBuffers`). Injected append failures, truncated-tail recovery, checksum rejection, and backup/restore verification are covered. A true multi-process contention test, concurrent ContextGraph/ChaosClusterLedger handle mutation safety, platform-specific power-loss fault testing, fuzzing and sanitizer runs remain outstanding. |
| P2 | Performance | Unverified | No declared-hardware measurement proves 30 MiB model integration, RTF <0.01, p99 latency or peak RSS for the v1.0 path. |

## Implementation order

1. Extend observation ownership to member salience, association confidence and
   PatternHypothesis confidence, preserving independent scorer lineage.
2. Add member membership status and append-only unmembership/reinstatement.
3. Add PatternHypothesis review/exclusion rationale and association valid-time.
4. Implement score-aware retrieval with independently configurable novelty,
   salience and diversity constraints, explicit ranking rationale and omissions.
5. Close all CC01-CC20 assertions, including negative and restart cases.
6. Complete merge/reactivation, retention manifests/tombstones, and operational
   hardening before production claims.

## First implementation tranche

The score-observation persistence work is being delivered separately from this
audit. It is intentionally limited to the native C++ ledger first. It does not
claim full score observation conformance until the C ABI/Python adapters,
bounded projection and score-aware retrieval are implemented and tested.


## Follow-up: score observation public API

The cluster-level score observation operation is now exposed through the C ABI
and Python `SemanticCoreBridge.record_chaos_score_observation`. Bounded JSON
returns the latest value in the schema-compatible `observations` object and
history revisions in `observation_history`, with offsets and omission counts.
Validation includes finite [0,1] values, required scorer/policy/timestamp
metadata, dimension allow-list, response cap, and restart replay tests.


## Follow-up: membership lifecycle

Member membership status and active/inactive state are now append-only,
version-checked journal mutations. The CC4 extension is optional and legacy
snapshots remain readable with the default active/UNCLASSIFIED state. The C ABI
and Python bridge expose removal and reinstatement; bounded projection reports
membership-history offsets and omission counts. Full adversarial and migration
coverage remains part of conformance closure.


## Follow-up: entity-scoped score observations

The CC5 extension adds independent member Salience, association Confidence,
and PatternHypothesis Confidence histories. A generic entity observation write
is available through C ABI and Python. Bounded projections expose latest
schema-compatible observations, history offsets, and omission counts. Replay
and API tests cover these paths; score-aware retrieval remains outstanding.


## Follow-up: association validity

Association validity bounds are persisted in CC6 and projected in bounded JSON.
The original C ABI input layout remains unchanged; temporal proposals use a
separate versioned input structure. A caller-supplied UTC as-of timestamp drives
an atomic expiry transaction, with append-only status/provenance history. The
Core does not consult a hidden wall clock; scheduling and invocation remain
host responsibilities.


## Follow-up: PatternHypothesis auditability

CC7 persists review status, epistemic-state revisions, per-member exclusion
rationales, and provenance. C ABI/Python review operations enforce explicit
excluded-member references and terminal review constraints. Bounded projection
includes capped revision history and omission metadata.


## Follow-up: lifecycle and merge

CC8 persists lifecycle transitions with reason and provenance. Merge is a
single version-checked transaction that archives sources, links lineage, and
preserves their members, associations, hypotheses and score histories.
Conflicting identities or divergent lifecycle-sensitive state reject the
transaction. The C ABI and Python bridge expose merge. Reactivation remains an
explicit audited lifecycle transition from ARCHIVED to DORMANT.


## Follow-up: typed query dimensions

`ContextRetriever.retrieve_chaos_cluster` now supports association type/status,
UTC valid-at, member admission-time bounds, producer provenance, member refs,
hypothesis/counterexample controls, and bounded lineage-depth traversal. It
returns query parameters, source omission metadata, unresolved/omitted lineage
refs and a state-version consistency check. Lineage metadata reads use zero
content budgets so linked clusters do not silently expand the root payload.


## Follow-up: ranking and pagination

The typed Chaos retrieval API now supports independent offsets and positive
page limits for members, associations and PatternHypotheses. It exposes
per-collection totals/offsets/next offsets and distinguishes terminal-page
completeness from full-dataset completeness. Ranking policies are explicit:
source order, score then stable ID, or reason/type/status diversity followed by
score. Member Salience, Association Confidence and PatternHypothesis Confidence
are used only for ordering; missing scores remain last and scores do not alter
epistemic status or evidence relations. Regression tests cover score ordering,
pagination, terminal-page semantics, out-of-range offsets and invalid budgets.


## Follow-up: auditable retention manifests

The Python retention planner now emits deterministic ACTIVE/ARCHIVED decisions
with factor-level explanations, source digests, provenance and lineage refs.
Coherence/organization is excluded from eviction scoring. Manifests are
canonical-digest verified and atomically written. Tombstone proposals are
explicit, actor/reason/policy-bound, manifest-bound events and do not claim
physical deletion. The authoritative EventJournal is never rewritten by this
module. Core journal integration, payload compaction and restore drills remain
open; see `CHAOS_CLUSTER_MEMORY_RETENTION_v0.1.md`.


## Follow-up: CC semantic regression closure

The native Chaos ledger regression suite now adds explicit checks for: co-membership
without inferred associations (CC02), coexistence of incompatible hypotheses
under a provisional contradiction edge (CC04), distinct producer provenance
for disagreeing recognizers (CC06), stale subset promotion rejection without
partial state change (CC10), changed payload under a reused idempotency identity
(CC12), and provenance-bound ARCHIVED -> DORMANT reactivation followed by new
membership (CC19). These are regression tests, not a claim that all CC01-CC20
cases are closed. CC14 and CC15 now have shared-EventJournal end-to-end negative and restart assertions; the broader CC01-CC20 matrix remains incomplete.


## Follow-up: CC14 / CC15 lifecycle isolation

A shared-EventJournal regression now exercises ContextGraph and
ChaosClusterLedger together. Reviewing a PatternHypothesis to
`PROMOTED_TO_EVIDENCE_REVIEW` does not create a Claim node or populate the
Cluster promotion map. Resolving a QuestionAspect does not change the Chaos
Cluster lifecycle or lifecycle history. The test closes and reopens the shared
journal and verifies that both domains replay independently. This closes the
specific CC14/CC15 regression gap, but does not close the remaining CC01-CC20
matrix.


## Follow-up: EventJournal implementation fact-check

The original durability paragraph in `EVENT_JOURNAL_v0.1.md` understated the
implementation: each appended frame is flushed and synchronized with
`fsync` (POSIX) or `FlushFileBuffers` (Windows). The writer lock is also
cross-process at the OS level (`flock` on POSIX and exclusive sharing on
Windows), although the automated regression suite still needs a true
multi-process contention test. This does not constitute a universal power-loss
guarantee; filesystem and hardware behavior still require platform-specific
fault testing.

## Retention-to-EventJournal integration

The retention planner now has a restricted C ABI transaction surface and a
Python bridge. Manifests and tombstone proposals are persisted as append-only
journal events under the global transaction/idempotency model. Replay rebuilds
the retention projection and rejects digest mismatch, event-ID mismatch,
missing manifest references, non-archived tombstone targets, and source digest
mismatch. The C ABI allowlists only the two implemented retention event kinds.
This does not compact the journal or perform physical deletion. Remaining
work includes snapshot/segment compaction, restore drills against semantic
projections, authenticated/off-host backup, crash injection at sync boundaries,
and power-loss validation.


## EventJournal retention integration and recovery validation

Retention manifests and tombstone proposals are now persisted through a
retention-only C ABI and Python bridge as atomic, idempotent EventJournal
transactions. Replay validates manifest structure and digest, deterministic
ranking/capacity assignment, event-ID binding, tombstone digest, archived
record membership, and source digest agreement. Regression coverage includes
idempotent retry, restart replay, rejection of non-retention event kinds, and
backup/restore followed by full Core open and retention projection replay.

A snapshot/compaction design contract has been added. It deliberately keeps
compaction disabled until every journal consumer has a versioned snapshot and
replay reducer and differential replay proves state equivalence. Current work
does not physically delete payloads or rewrite the authoritative journal.


## Shared-journal replay ownership

ContextGraph replay now explicitly advances its global State Version across
`RETENTION_MANIFEST_RECORDED` and `RETENTION_TOMBSTONE_PROPOSED` events without
interpreting those payloads as graph mutations. This is an explicit
cross-domain event registration, not a permissive ignore-unknown fallback.
A regression opens and reopens a journal containing both retention event kinds
and verifies graph state remains unchanged while the global version advances.


The same replay audit found that standalone `EVIDENCE_PROVENANCE_SET_V1`
events also require explicit version-only handling in ContextGraph. The graph
now advances the shared State Version for this evidence-owned event without
applying it as graph state. The regression covers both retention event kinds
and this pre-existing external owner event.


## EventJournal commit uncertainty

EventJournal now exposes a sync-barrier fault-injection seam. A failed sync
before the COMMIT marker is verified as an incomplete transaction that replay
discards. A failed sync after the COMMIT marker write returns
`JOURNAL_COMMIT_OUTCOME_UNKNOWN`; recovery plus an exact idempotent retry
resolves both cases: marker survived (duplicate) and marker was lost (new
commit). The tests model the loss of an unsynchronized marker by truncating to
the last synchronized EVENT frame. This is deterministic fault injection, not
a physical power-loss simulation.


## Performance baseline tooling

An opt-in EventJournal benchmark target now measures append p50/p95/p99,
replay time, journal size, and event count using real platform durability
barriers. CI runs a small smoke workload only; it is not a performance gate.
This does not measure recognition-model inference, RTF, peak RSS, or the
30MB model target. Those require a separate adapter and target-device run.
