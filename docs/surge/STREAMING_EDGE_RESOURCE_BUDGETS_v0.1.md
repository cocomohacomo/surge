# Streaming Edge Resource Budgets v0.1

Status: packet admission, journal write facade and language resident/scratch reservations are implemented on the feature branch; ContextGraph-wide accounting and target RSS enforcement remain incomplete.

## 1. Priority and invariants

SURGE is streaming-native. Resource protection is therefore a correctness property, not a post-hoc optimization. The Core MUST preserve forward progress when a budget is reached. It MUST NOT respond to memory pressure by silently dropping the current input, partially committing a decision, or allowing a language pack to allocate outside its declared budget.

The implementation order is: (1) bound incoming events and decision packets before materialization; (2) reserve transient working memory before candidate generation and release it after the decision transaction; (3) bound resident journal, graph, cluster, evidence and language-resource memory; (4) apply relevance-aware forgetting to eligible history; (5) protect active Focus, unresolved Questions, temporal dependencies and provenance ahead of inactive material; (6) defer/reject on exhaustion with stable reason codes.

## 2. Resource dimensions

Host profiles supply independent limits for resident bytes, transient bytes, retained events, retained clusters, decision candidates/evidence, surface sets/hypotheses/trace entries, source and rendered string bytes, package and unpacked bytes, language-resource residency/scratch, event/output bytes, lattice nodes/arcs, n-gram work and cache bytes. Zero means zero capacity. Unlimited capacity must be explicit.

## 3. Reservation lifecycle

The ResourceBudget / ResourceLease primitive provides mutex-protected, overflow-safe logical reservations. BudgetedPacketAdmissionCoordinator reserves packet staging, retained packet bytes and event slots before durable packet commit. BudgetedEventJournal provides the corresponding guarded journal write path and must be used by runtime mutation paths rather than calling EventJournal::commit directly. LanguageRuntimeRegistry can share the same ResourceBudget to reserve installed-set resident bytes and session-pinned scratch capacity. Leases are move-only and release reservations on destruction or reset. The budget must outlive its leases. ResourceKind currently covers resident bytes, transient bytes, retained events, retained clusters, decision candidates and decision evidence. This is accounting only: allocations are protected only after the owning subsystem acquires and holds the corresponding lease.

Logical reservations are not allocator overhead, fragmentation, native-library allocations or process RSS. Host benchmarks MUST still measure peak RSS on target hardware.

## 4. Decision packet bounds

DecisionPolicy bounds candidates/evidence, family weights, surface sets, aggregate surface hypotheses and trace entries, source-ref bytes and rendered-surface bytes. Invalid numeric thresholds are rejected. Evidence with zero calibrated contribution cannot authorize selection alone. Exhaustion returns DEFER with deterministic reason codes.

The C ABI in surge/decision_c_api.h accepts flat caller-owned arrays with validated offset/count ranges. This avoids requiring language bindings to construct nested native object graphs. The ABI reports the selected candidate and exact selected surface-hypothesis indices. It does not transfer ownership of caller buffers.

## 5. Streaming admission and retention

An event is admitted only after validating encoded size and reserving transient and resident budget. BudgetedEventJournal reserves the complete transaction before EventJournal::commit; budget failure leaves the journal watermark unchanged. The transaction resident reservation is retained until the facade is destroyed. Recovery rebuilds reservations from the committed journal and fails closed if the configured budget cannot represent it. A committed event consumes resident budget until retention policy releases or compacts its in-memory representation. Journal durability and in-memory residency are separate concerns.

When resident budget is exhausted, the Core evaluates eligible forgetting candidates by relevance, active Focus, Question status, temporal validity, dependency impact, evidence provenance and recovery cost. It must not evict the only current representation of an active decision, unresolved contradiction or required provenance chain. If no safe victim exists, defer the new operation rather than exceeding the cap.

## 6. Language Set resource isolation

A Language Set cannot bypass Core reservations. Package validation checks compressed/unpacked size, ABI compatibility, resource maxima, digest, capabilities and license/provenance metadata before activation. Loading is transactional: failed activation leaves the prior session-pinned set usable. Caches are bounded and evictable independently of immutable journal provenance.

Data-only packs are preferred first. Native extensions are optional and should support process isolation where required. Network access is prohibited in the default edge profile.

## 7. Verification gates

Automated tests must cover exact-capacity success and over-capacity failure; arithmetic overflow; move/reset/destruction accounting; failed reservations preserving active leases and counters; concurrent reservations; decision exhaustion without partial surfaces; cluster failure without delta mutation; failed pack activation preserving the active set; and retention protecting Focus/Question/provenance state.

Target measurements must include peak RSS and resident high-water, transient peak, p50/p95/p99 per-event latency and queue delay, sustainable throughput, package load/switch latency and cache hit ratio, and sustained overload when the consumer is slower than the producer.

No edge-PC memory or latency claim is valid until measured on specified hardware with the complete recognition + normalization + Core pipeline.


## 8. Current implementation boundary

The current BudgetedEventJournal and BudgetedPacketAdmissionCoordinator are opt-in facades. Existing code paths that call EventJournal::commit directly are not automatically protected. Runtime integration must route every mutation through the budgeted facade before claiming end-to-end enforcement. ContextGraph node/relation/evidence memory, dependency indexes, question ledgers, snapshot restore peak memory and allocator overhead still require dedicated accounting and tests. LanguageRuntimeRegistry reservations cover manifest-declared resident bytes and one session scratch reservation per pinned session; they do not themselves load, hash or verify package files.


## ContextGraph admission increment (feature branch)

An opt-in `BudgetedContextGraph` facade now reserves logical resident bytes and
retained-cluster slots before applying a `SemanticDelta`. It estimates bounded
costs for nodes, relations, scope spans, validity records and focus references.
When configured with a staging limit, it also reserves transient bytes before
preparing the transaction. Budget exhaustion rejects the delta before the
journal state version advances. Recovery can rebuild the facade's baseline
reservation from the already-restored graph.

This remains an opt-in admission boundary: callers must route every semantic
delta through `BudgetedContextGraph::apply`. Direct calls to
`ContextGraph::apply` and the separate question-memory/status mutators bypass
this guard. The current logical estimator does not yet include all historical
indexes, allocator fragmentation, journal buffers, snapshot restore peak
memory, or process RSS. It is a conservative per-delta estimate, not a hard RSS
cap. Full integration must move the facade to the Core-owned mutation boundary
and cover all graph state transitions before claiming global enforcement.


## Guarded Question and reassessment mutations

The opt-in `BudgetedContextGraph` now exposes guarded wrappers for Question
reference/reactivation, dependent-question reactivation, Question memory-state
changes, capacity demotion, QuestionAspect status changes, and temporal claim
reassessment. These wrappers reserve logical resident bytes and retained-event
slots before the underlying journal mutation; bounded cascades and batches also
reserve transient staging bytes when a staging ceiling is configured. Failed
reservations leave the graph state version unchanged.

Recovery accounting now includes Focus history, Question state/reference
records, Claim/Evidence validity history, dependency evaluation records and
temporal reassessment history in addition to nodes, relations and scope spans.
The estimator remains logical and conservative. It does not yet account for
every STL index allocation, journal internals, all snapshot staging memory,
allocator fragmentation, or process RSS.

These wrappers do not intercept direct calls to the corresponding public
`ContextGraph` methods. Core integration and migration of every call site
remain required before global enforcement can be claimed.


## Atomic cluster classification through the budget gate

`BudgetedContextGraph::append_cluster_membership` now combines Core-owned
cluster-ID derivation, parent/member relation construction and budgeted journal
commit in one API. Callers provide classification hints and a base delta; the
facade asks `ContextGraph` to construct the typed membership and then sends the
completed delta through the same resident, transient, retained-event and
retained-cluster admission checks as ordinary graph updates.

If the budget reservation fails, the generated cluster ID is cleared and no
graph version, node or membership relation is committed. This closes one
important bypass for callers that previously could classify into a mutable
`SemanticDelta` and then accidentally commit it through a direct graph path.
Other direct `ContextGraph` mutators remain public and are still outside the
guard; this increment is not yet end-to-end Core enforcement.


The recovery estimator now performs each size addition as a separately checked
operation. This avoids overflow in intermediate expressions before the final
budget accumulator is checked, including on 32-bit `size_t` targets. This is
still a logical estimate and does not replace process RSS measurement.


## Language manifest digest validation increment

`LanguageRuntimeRegistry::install_manifest` now rejects a manifest digest unless it is exactly 64 hexadecimal characters, matching the SHA-256 field contract in the package schema. The registry validates digest syntax only; it does not read package files or recompute resource hashes. Package loading, path containment, decompression limits, per-resource hash verification and transactional activation remain unimplemented and are still required before a package can be trusted or executed.


## P0 completion gate (2026-11-19)

P0 is **not complete** until every gate below is satisfied. Existing opt-in facades are useful admission primitives but do not constitute global enforcement.

### P0-A: Core mutation boundary
- [ ] Make the Core-owned mutation gateway the only supported production write path for ContextGraph, Question, QuestionAspect, Focus, Claim/Evidence temporal state and cluster membership.
- [ ] Migrate every runtime, adapter, recovery and maintenance call site to the gateway.
- [ ] Prevent accidental bypass: production-facing graph mutators must be private/internal or require an explicit unbudgeted test-only construction mode. A documentation convention is insufficient.
- [ ] Verify failure atomicity: budget exhaustion must leave journal watermark, graph state version, graph maps, indexes and idempotency state unchanged.

### P0-B: Runtime working-set reservations
- [ ] Reserve transient memory before input decoding/materialization, candidate generation, morphology, lattice expansion, n-gram lookup, normalization and serialization.
- [ ] Propagate one shared budget and lease through C++ and Python/ONNX boundaries; release leases on every success, error, cancellation and exception path.
- [ ] Bound candidate/lattice/ngram/cache/output work independently. Reject or defer before allocation when the reservation cannot be acquired.
- [ ] Exercise overload with producer faster than consumer and prove bounded queueing/backpressure without silent input loss.

### P0-C: Memory lifecycle
- [ ] Implement expiry-aware pruning and relevance-ranked forgetting for eligible history, with deterministic tie-breaking.
- [ ] Protect active Focus, unresolved Questions, current claims, required temporal dependencies and the last provenance path.
- [ ] Add bounded cluster compaction, tombstones and replay-safe deletion semantics. Never mutate immutable journal provenance in place.
- [ ] If the protected floor itself exceeds capacity, return a stable fail-closed result and preserve state rather than evicting protected records.

### P0-D: Snapshot and recovery peak control
- [ ] Validate snapshot byte/count limits before materializing decoded collections.
- [ ] Reserve transient staging for the complete restore candidate plus replay tail before constructing it.
- [ ] Keep old live state intact until all domains validate and the aggregate restore reservation succeeds; commit atomically.
- [ ] Release old-state reservations only after successful swap; test failed restore rollback and peak reservation accounting.

### P0-E: Integration and evidence
- [ ] Replace opt-in construction with Core-owned wiring so production code cannot instantiate an unguarded mutation path accidentally.
- [ ] Add integration tests proving every write path is budgeted, including all question/status/temporal/snapshot flows.
- [ ] Run C++ conformance, ASan/UBSan, fuzz smoke and Windows/Linux/macOS CI on the latest head.
- [ ] Measure complete-pipeline peak RSS, transient high-water, p50/p95/p99 latency, queue depth and sustained overload on the specified edge PC. Logical byte estimates alone do not satisfy this gate.

### Current status

The branch currently has budget primitives and opt-in facades for packet admission, event journal, ContextGraph deltas and selected Question/temporal mutations. It also has protected Question demotion and checked recovery estimates. The following P0 gates remain open: production call-site migration and bypass prevention; reservations around all recognition/morphology/n-gram/ONNX allocations; full expiry-aware forgetting and cluster compaction; aggregate snapshot restore staging reservations; full-pipeline integration tests; and target edge-PC measurements. Therefore this document and PR must not describe P0 as complete until every gate above has evidence attached.
