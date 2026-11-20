#include "surge/resource_budget.hpp"
#include <cassert>
#include <limits>
#include <string>
#include <utility>

int main() {
  using namespace surge;
  ResourceBudget budget({1024, 512, 10, 4, 8, 32});
  std::string error;
  ResourceLease resident;
  assert(budget.try_acquire(ResourceKind::ResidentBytes, 768, resident, error));
  assert(budget.snapshot().used[0] == 768);
  assert(!budget.try_acquire(ResourceKind::ResidentBytes, 1, resident, error));
  assert(error == "RESOURCE_LEASE_ALREADY_ACTIVE");
  assert(budget.snapshot().used[0] == 768);
  ResourceLease too_large;
  assert(!budget.try_acquire(ResourceKind::ResidentBytes, 257, too_large, error));
  assert(error == "RESOURCE_BUDGET_EXCEEDED");
  {
    ResourceLease transient;
    assert(budget.try_acquire(ResourceKind::TransientBytes, 512, transient, error));
    ResourceLease moved(std::move(transient));
    assert(!transient && moved);
    assert(budget.snapshot().used[1] == 512);
  }
  assert(budget.snapshot().used[1] == 0);
  ResourceLease overflow;
  assert(!budget.try_acquire(ResourceKind::RetainedEvents,
      std::numeric_limits<std::size_t>::max(), overflow, error));
  assert(budget.snapshot().used[2] == 0);
  resident.reset();
  assert(budget.snapshot().used[0] == 0);
  ResourceLease partial;
  assert(budget.try_acquire(ResourceKind::ResidentBytes, 400, partial, error));
  assert(partial.amount() == 400);
  assert(!partial.shrink(401));
  assert(budget.snapshot().used[0] == 400);
  assert(partial.shrink(150));
  assert(partial.amount() == 250);
  assert(budget.snapshot().used[0] == 250);
  assert(partial.shrink(250));
  assert(!partial);
  assert(budget.snapshot().used[0] == 0);
  assert(partial.shrink(0));

  // The temporary edge profile caps ResidentBytes + TransientBytes
  // together at 200 MiB, rather than allowing each category 200 MiB.
  constexpr std::size_t kMiB = 1024U * 1024U;
  ResourceBudget edge_budget(ResourceBudgetLimits::temporary_edge_200mb());
  ResourceLease edge_resident;
  ResourceLease edge_transient;
  assert(edge_budget.try_acquire(ResourceKind::ResidentBytes,
      150U * kMiB, edge_resident, error));
  assert(edge_budget.try_acquire(ResourceKind::TransientBytes,
      50U * kMiB, edge_transient, error));
  const auto edge_snapshot = edge_budget.snapshot();
  assert(edge_snapshot.total_byte_used == 200U * kMiB);
  assert(edge_snapshot.total_byte_limit == 200U * kMiB);
  ResourceLease over_edge;
  assert(!edge_budget.try_acquire(ResourceKind::TransientBytes,
      1, over_edge, error));
  assert(error == "RESOURCE_TOTAL_BYTE_BUDGET_EXCEEDED");
  edge_transient.reset();
  assert(edge_budget.try_acquire(ResourceKind::TransientBytes,
      1U * kMiB, over_edge, error));
  assert(edge_budget.snapshot().total_byte_used == 151U * kMiB);

  return 0;
}
