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
  return 0;
}
