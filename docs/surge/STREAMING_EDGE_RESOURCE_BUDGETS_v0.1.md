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
