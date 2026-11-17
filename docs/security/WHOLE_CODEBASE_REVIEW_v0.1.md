# SURGE Whole-Codebase Review v0.1

- Baseline: main at 0dca94a63a7288b4e53e7eaefc42f2af0280c04c
- Date: 2026-11-17
- Scope: 206 tracked files; C++ Core, journal, packet admission, context/dependency/temporal ledgers, C ABI; Python adapters and ONNX boundary; schemas, CI, tests and security/operations docs.
- Method: static source review and cross-layer invariant tracing. Not a formal penetration test, fuzz campaign, race-detector run or independent cryptographic audit.

## Findings

### [P1] Packet ID duplicate detection trusted a caller-supplied digest
Location: src/packet_admission.cpp, PacketAdmissionCoordinator::admit.

For an existing packet ID, Core compared only canonical_request_digest supplied at the C ABI boundary. Reusing that digest while changing source generation, sequence, block/range identity or revision lineage could make a different envelope appear to be a duplicate. Python's local adapter compares the canonicalized packet itself, but the durable C++ coordinator did not.

Remediation in this change: retain the exact encoded durable envelope during replay and admission. Duplicate acceptance now requires both digest and complete encoded envelope equality. Added a regression case reusing the original digest while changing packet identity fields.

### [P1] Core handle is not safe for concurrent callers
Locations: include/surge/context_graph.hpp, src/c_api.cpp and adapter bridges.

EventJournal serializes its own methods and the sidecar lock excludes other writer processes. ContextGraph, PacketAdmissionCoordinator, TemporalBoundaryLedger, TemporalQuestionLedger and aggregate C ABI Handle expose mutable maps/vectors without a shared per-handle lock. Concurrent calls can race during prepare/replay, read-modify-write and post-commit projection updates. The journal mutex does not make higher-level transactions atomic.

Mitigation: one serialized writer per Core handle is the current safe operating contract. Do not share a handle concurrently across threads. Add a per-handle transaction mutex, snapshot-safe reads and ThreadSanitizer stress tests before advertising thread-safe use.

### [P1] Native input size limits are incomplete
Locations: src/c_api.cpp packet and semantic entry points.

Several collection counts are bounded and EventJournal frames are capped at 16 MiB, but source snapshot size and many individual C strings (content, identifiers, labels, provenance and serialized proposal data) are not uniformly bounded before copying into std::string or constructing SemanticDelta. Oversized input can consume memory before journal serialization rejects it.

Mitigation: impose a shared maximum source snapshot, per-field UTF-8 byte limits, total delta byte budget and retrieval output budget at the C ABI boundary. The frame limit alone is too late to serve as an input limit.

### [P2] Journal checksums and idempotency fingerprints are not tamper evidence
Location: src/event_journal.cpp.

CRC32 detects accidental frame corruption; FNV-1a detects ordinary duplicate request mismatches. Neither authenticates a journal against an actor able to rewrite the file and recompute checksums. Treat this as crash-recoverable local persistence, not an adversarially tamper-proof audit log. A keyed MAC/signature and protected key management are required for that threat model.

### [P2] Some adapter-generated identifiers use short non-cryptographic hashes
Locations: surge_adapter/context_retrieval.py and scoped question routing.

FNV-1a-derived identifiers are deterministic compact keys, not collision-resistant identities. They must not prove content identity. Use SHA-256 over unambiguous canonical encoding or retain and compare the full preimage on collision.

### [P2] Retrieval and model output are not a security boundary
Retrieval ranking is a lexical projection. LLM/ONNX candidates and ContextHint associations are proposals, not verified claims. Consumers must preserve provenance, validate spans and relation endpoints, and must not interpret a high score as truth or authorization.

### [P2] Platform durability needs fault-injection validation
EventJournal has platform-specific durability paths. Windows directory metadata durability is not equivalent to POSIX parent-directory fsync. CI functional tests do not prove power-loss durability. Add crash injection and restore drills on Windows/macOS/Linux.

## Verified defensive controls
- C++ recomputes packet source snapshot and range SHA-256.
- Byte offsets are checked against normalized snapshot bounds.
- Journal frame size is capped at 16 MiB and each frame has CRC32.
- Journal uses a lifetime sidecar writer lock.
- Reviewed C ABI collection counts are bounded in semantic and ticket APIs.
- JSON Schema rejects unknown packet fields.
- Python packet adapter checks canonical packet digest and NFC byte ranges.
- Recovery replays committed transactions and truncates incomplete tail writes.

## Review limitations and next hardening work
This review did not execute ASan/UBSan/TSan, AFL/libFuzzer, hostile filesystem tests, dependency supply-chain audit or formal privacy/legal review. It does not prove absence of vulnerabilities.

Priority: per-handle serialization; uniform C ABI byte budgets and NUL/UTF-8 validation; fuzz journal decoding, packet admission and ScopeSpan validation; authenticated journal checkpoints; collision-safe canonical identifiers; cross-platform crash-injection and restore drills.
