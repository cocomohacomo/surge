#pragma once
#include <cstdlib>
#include <iostream>
namespace surge::test {
inline void check(bool passed, const char* expression, const char* file, int line) {
  if (passed) return;
  std::cerr << file << ":" << line << ": CHECK failed: " << expression << std::endl;
  std::exit(EXIT_FAILURE);
}
}  // namespace surge::test
#define CHECK(...) ::surge::test::check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __FILE__, __LINE__)
