#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace surge {
enum class GapState { Open, ResolvedByPacket, AuthorizedSkip, Reopened };
enum class ScopeState { Open, Unresolved, Closed, Invalidated };
enum class Gate { Pass, Fail, Inconclusive };
struct IngestResult { bool duplicate{false}; std::string error; };
struct Gap { std::uint64_t sequence{}; GapState state{GapState::Open}; std::string skip_key; };

class RangeLedger {
 public:
  IngestResult ingest(const std::string& range_key, const std::string& digest,
                      const std::string& idempotency_key, bool revision=false,
                      std::uint64_t revision_sequence=0);
  std::size_t evidence_count() const { return evidence_count_; }
  std::size_t revision_count() const { return revision_count_; }
 private:
  struct Entry { std::string digest; std::string idempotency_key; std::string request_digest; std::uint64_t revision_sequence{0}; };
  // A vector is append-only history; a correction never overwrites the prior Evidence record.
  std::map<std::string, std::vector<Entry>> ranges_;
  std::map<std::string, std::string> keys_;
  std::size_t evidence_count_{0};
  std::size_t revision_count_{0};
};

class SourceSequencer {
 public:
  explicit SourceSequencer(std::uint64_t initial_watermark=0) : watermark_(initial_watermark) {}
  void observe(std::uint64_t sequence);
  bool authorize_skip(std::uint64_t sequence, const std::string& actor_capability,
                      const std::string& reason_code, const std::string& policy_version,
                      const std::string& idempotency_key);
  bool ingest_late(std::uint64_t sequence);
  std::string packet_status(std::uint64_t sequence) const;
  std::uint64_t watermark() const { return watermark_; }
  const std::map<std::uint64_t, Gap>& gaps() const { return gaps_; }
  std::size_t audit_count() const { return audit_count_; }
 private:
  void advance();
  std::uint64_t watermark_{};
  std::set<std::uint64_t> packets_;
  std::map<std::uint64_t, Gap> gaps_;
  std::map<std::string, std::string> skip_requests_;
  std::size_t audit_count_{0};
};

class ScopeSpan {
 public:
  explicit ScopeSpan(std::string kind) : kind_(std::move(kind)) {}
  bool close(bool opener_anchored, bool closer_anchored, bool target_bound);
  void mark_unresolved();
  bool invalidate(const std::string& revision_ref);
  ScopeState state() const { return state_; }
 private:
  std::string kind_;
  ScopeState state_{ScopeState::Open};
  std::vector<std::string> revisions_;
};

struct TicketResult { bool resolved{false}; bool stale{false}; std::uint64_t state_version{}; };
TicketResult resolve_ticket(std::uint64_t ticket_version, std::uint64_t current_version,
                            bool query_replayable);
Gate quality_gate(std::size_t eligible, std::size_t accepted, double minimum_coverage);
Gate performance_gate(double measured_rtf, double maximum_rtf);
Gate overall_gate(Gate semantic, Gate performance, std::size_t invariant_violations);
}  // namespace surge
