#pragma once
#include <string>
namespace surge {
// SHA-256 over the exact byte sequence. Returns 64 lowercase hexadecimal chars.
std::string sha256_hex(const std::string& bytes);
// Strict UTF-8 validator. Does not normalize; callers must supply NFC bytes.
bool is_valid_utf8(const std::string& bytes);
}  // namespace surge
