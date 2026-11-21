#include <surge/c_api.h>

int main() {
  // This consumer intentionally checks the installed public header and
  // transitive shared-library link without opening a runtime journal.
  SurgeResourceBudgetLimits limits{};
  return limits.resident_bytes == 0 ? 0 : 1;
}
