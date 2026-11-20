#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace surge {

// Incremental SHA-256. update() accepts arbitrary chunk boundaries and uses
// fixed-size internal storage. finalize_hex() is idempotent; update() after
// finalization is rejected.
class Sha256Hasher {
 public:
  Sha256Hasher() noexcept;
  bool update(const void* data, std::size_t size) noexcept;
  bool update(const std::string& bytes) noexcept {
    return update(bytes.data(), bytes.size());
  }
  std::string finalize_hex();

 private:
  void transform(const std::uint8_t* block) noexcept;
  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_{0};
  std::uint64_t total_bytes_{0};
  bool finalized_{false};
  std::string digest_;
};

// SHA-256 over the exact byte sequence. Returns 64 lowercase hexadecimal chars.
std::string sha256_hex(const std::string& bytes);
// Strict UTF-8 validator. Does not normalize; callers must supply NFC bytes.
bool is_valid_utf8(const std::string& bytes);

}  // namespace surge
