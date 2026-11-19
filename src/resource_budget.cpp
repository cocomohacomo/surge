#include "surge/resource_budget.hpp"
#include <utility>

namespace surge {
namespace {
constexpr std::size_t index(ResourceKind kind) {
  return static_cast<std::size_t>(kind);
}
}

std::array<std::size_t, static_cast<std::size_t>(ResourceKind::Count)>
ResourceBudget::to_array(const ResourceBudgetLimits& limits) {
  return {limits.resident_bytes, limits.transient_bytes, limits.retained_events,
          limits.retained_clusters, limits.decision_candidates,
          limits.decision_evidence};
}

ResourceBudget::ResourceBudget(ResourceBudgetLimits limits)
    : limits_(to_array(limits)) {}

bool ResourceBudget::try_acquire(ResourceKind kind, std::size_t amount,
                                 ResourceLease& lease, std::string& error) {
  error.clear();
  const auto slot = index(kind);
  if (slot >= limits_.size()) {
    error = "RESOURCE_KIND_INVALID";
    return false;
  }
  if (lease) {
    error = "RESOURCE_LEASE_ALREADY_ACTIVE";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (amount > limits_[slot] || used_[slot] > limits_[slot] - amount) {
    error = "RESOURCE_BUDGET_EXCEEDED";
    return false;
  }
  used_[slot] += amount;
  lease = ResourceLease(this, kind, amount);
  return true;
}

ResourceBudgetSnapshot ResourceBudget::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return {used_, limits_};
}

void ResourceBudget::release(ResourceKind kind, std::size_t amount) noexcept {
  const auto slot = index(kind);
  if (slot >= used_.size()) return;
  std::lock_guard<std::mutex> lock(mutex_);
  used_[slot] = amount > used_[slot] ? 0 : used_[slot] - amount;
}

ResourceLease::~ResourceLease() { reset(); }

ResourceLease::ResourceLease(ResourceLease&& other) noexcept
    : owner_(other.owner_), kind_(other.kind_), amount_(other.amount_) {
  other.owner_ = nullptr;
  other.amount_ = 0;
}

ResourceLease& ResourceLease::operator=(ResourceLease&& other) noexcept {
  if (this != &other) {
    reset();
    owner_ = other.owner_;
    kind_ = other.kind_;
    amount_ = other.amount_;
    other.owner_ = nullptr;
    other.amount_ = 0;
  }
  return *this;
}

void ResourceLease::reset() noexcept {
  if (owner_) owner_->release(kind_, amount_);
  owner_ = nullptr;
  amount_ = 0;
}

}  // namespace surge
