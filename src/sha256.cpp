#include "surge/sha256.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace surge {
namespace {
constexpr std::array<std::uint32_t,64> K{{
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
  return (x >> n) | (x << (32 - n));
}
constexpr std::array<std::uint32_t, 8> kInitialState{{
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};
}  // namespace

Sha256Hasher::Sha256Hasher() noexcept : state_(kInitialState) {}

void Sha256Hasher::transform(const std::uint8_t* block) noexcept {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t i = 0; i < 16; ++i) {
    const auto j = i * 4;
    w[i] = (std::uint32_t(block[j]) << 24) |
           (std::uint32_t(block[j + 1]) << 16) |
           (std::uint32_t(block[j + 2]) << 8) | block[j + 3];
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                    (w[i - 15] >> 3);
    const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                    (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  auto a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  auto e = state_[4], f = state_[5], g = state_[6], z = state_[7];
  for (std::size_t i = 0; i < 64; ++i) {
    const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const auto ch = (e & f) ^ (~e & g);
    const auto t1 = z + s1 + ch + K[i] + w[i];
    const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const auto maj = (a & b) ^ (a & c) ^ (b & c);
    const auto t2 = s0 + maj;
    z = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
  state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += z;
}

bool Sha256Hasher::update(const void* data, std::size_t size) noexcept {
  if (finalized_ || (size != 0 && data == nullptr)) return false;
  if (size > std::numeric_limits<std::uint64_t>::max() / 8U - total_bytes_)
    return false;
  const auto* input = static_cast<const std::uint8_t*>(data);
  total_bytes_ += static_cast<std::uint64_t>(size);
  if (buffered_ != 0) {
    const auto take = std::min(size, buffer_.size() - buffered_);
    if (take != 0) std::copy(input, input + take, buffer_.begin() + buffered_);
    buffered_ += take;
    input += take;
    size -= take;
    if (buffered_ == buffer_.size()) {
      transform(buffer_.data());
      buffered_ = 0;
    }
  }
  while (size >= buffer_.size()) {
    transform(input);
    input += buffer_.size();
    size -= buffer_.size();
  }
  if (size != 0) {
    std::copy(input, input + size, buffer_.begin());
    buffered_ = size;
  }
  return true;
}

std::string Sha256Hasher::finalize_hex() {
  if (finalized_) return digest_;
  const auto bit_count = total_bytes_ * 8U;
  buffer_[buffered_++] = 0x80;
  if (buffered_ > 56) {
    std::fill(buffer_.begin() + buffered_, buffer_.end(), 0);
    transform(buffer_.data());
    buffered_ = 0;
  }
  std::fill(buffer_.begin() + buffered_, buffer_.begin() + 56, 0);
  for (int i = 7; i >= 0; --i)
    buffer_[56 + static_cast<std::size_t>(7 - i)] =
        static_cast<std::uint8_t>(bit_count >> (i * 8));
  transform(buffer_.data());
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const auto value : state_) out << std::setw(8) << value;
  digest_ = out.str();
  finalized_ = true;
  return digest_;
}

std::string sha256_hex(const std::string& input) {
  Sha256Hasher hasher;
  hasher.update(input);
  return hasher.finalize_hex();
}

bool is_valid_utf8(const std::string& s) {
  std::size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c <= 0x7f) {
      ++i;
      continue;
    }
    std::size_t n = 0;
    std::uint32_t cp = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      n = 2; cp = c & 0x1f;
    } else if (c >= 0xe0 && c <= 0xef) {
      n = 3; cp = c & 0x0f;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 4; cp = c & 0x07;
    } else {
      return false;
    }
    if (i + n > s.size()) return false;
    for (std::size_t j = 1; j < n; ++j) {
      const auto d = static_cast<unsigned char>(s[i + j]);
      if ((d & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (d & 0x3f);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) {
      return false;
    }
    i += n;
  }
  return true;
}
}  // namespace surge
