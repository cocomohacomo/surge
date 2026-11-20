#pragma once
#include <array>
#include <cstddef>
#include <mutex>
#include <string>

namespace surge {

enum class ResourceKind : std::size_t {
  ResidentBytes = 0,
  TransientBytes,
  RetainedEvents,
  RetainedClusters,
  DecisionCandidates,
  DecisionEvidence,
  Count
};

struct ResourceBudgetLimits {
  std::size_t resident_bytes{200U * 1024U * 1024U};
  std::size_t transient_bytes{200U * 1024U * 1024U};
  std::size_t retained_events{0};
  std::size_t retained_clusters{0};
  std::size_t decision_candidates{0};
  std::size_t decision_evidence{0};
  // Combined logical ceiling for ResidentBytes + TransientBytes; zero is unlimited.
  std::size_t total_byte_limit{200U * 1024U * 1024U};
  static ResourceBudgetLimits temporary_edge_200mb();
};

struct ResourceBudgetSnapshot {
  std::array<std::size_t, static_cast<std::size_t>(ResourceKind::Count)> used{};
  std::array<std::size_t, static_cast<std::size_t>(ResourceKind::Count)> limits{};
  std::size_t total_byte_used{0};
  std::size_t total_byte_limit{0};
};

// A lease accounts for a bounded resource reservation. The budget must outlive
// every lease created from it. This is logical accounting, not allocator RSS.
class ResourceBudget;
class ResourceLease {
 public:
  ResourceLease() = default;
  ~ResourceLease();
  ResourceLease(const ResourceLease&) = delete;
  ResourceLease& operator=(const ResourceLease&) = delete;
  ResourceLease(ResourceLease&& other) noexcept;
  ResourceLease& operator=(ResourceLease&& other) noexcept;
  void reset() noexcept;
  bool shrink(std::size_t amount) noexcept;
  bool grow(std::size_t amount, std::string& error) noexcept;
  std::size_t amount() const noexcept { return amount_; }
  explicit operator bool() const noexcept { return owner_ != nullptr; }

 private:
  friend class ResourceBudget;
  ResourceLease(ResourceBudget* owner, ResourceKind kind, std::size_t amount)
      : owner_(owner), kind_(kind), amount_(amount) {}
  ResourceBudget* owner_{nullptr};
  ResourceKind kind_{ResourceKind::ResidentBytes};
  std::size_t amount_{0};
};

class ResourceBudget {
 public:
  explicit ResourceBudget(ResourceBudgetLimits limits);
  bool try_acquire(ResourceKind kind, std::size_t amount,
                   ResourceLease& lease, std::string& error);
  ResourceBudgetSnapshot snapshot() const;

 private:
  friend class ResourceLease;
  void release(ResourceKind kind, std::size_t amount) noexcept;
  bool grow(ResourceKind kind, std::size_t amount, std::string& error) noexcept;
  static std::array<std::size_t, static_cast<std::size_t>(ResourceKind::Count)>
      to_array(const ResourceBudgetLimits& limits);
  mutable std::mutex mutex_;
  std::array<std::size_t, static_cast<std::size_t>(ResourceKind::Count)> limits_{};
  std::array<std::size_t, static_cast<std::size_t>(ResourceKind::Count)> used_{};
  std::size_t total_byte_limit_{0};
};

}  // namespace surge
