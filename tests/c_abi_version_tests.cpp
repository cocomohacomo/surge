#include "surge/c_api.h"

#include <cstdlib>
#include <iostream>

int main() {
  const uint32_t expected = SURGE_ABI_VERSION_ENCODE(
      SURGE_ABI_VERSION_MAJOR, SURGE_ABI_VERSION_MINOR,
      SURGE_ABI_VERSION_PATCH);
  const uint32_t actual = surge_abi_version();
  if (actual != expected) {
    std::cerr << "C ABI version mismatch: expected " << expected
              << ", got " << actual << '\\n';
    return EXIT_FAILURE;
  }
  if (SURGE_ABI_VERSION_MAJOR != 0u ||
      SURGE_ABI_VERSION_MINOR == 0u) {
    std::cerr << "Unexpected provisional ABI compatibility level\\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
