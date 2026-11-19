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

  return 0;
}
