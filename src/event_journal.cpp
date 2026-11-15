#include "surge/event_journal.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace surge {
namespace {
constexpr char kMagic[] = "SURGEJ1\n";
constexpr std::uint32_t kMaxFrame = 16U * 1024U * 1024U;

std::uint32_t crc32(const std::string& data) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (unsigned char byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}
void put_u32(std::ostream& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) out.put(static_cast<char>((value >> (i * 8)) & 0xFFU));
}
bool get_u32(std::istream& in, std::uint32_t& value) {
  value = 0;
  for (int i = 0; i < 4; ++i) {
    const int c = in.get();
    if (c == EOF) return false;
    value |= static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << (i * 8);
  }
  return true;
}
std::string request_fingerprint(const std::string& tx, std::uint64_t version,
                                const std::vector<JournalEvent>& events) {
  // Stable non-cryptographic FNV-1a fingerprint for idempotency comparison only.
  std::uint64_t h = 1469598103934665603ULL;
  auto mix = [&h](const std::string& s) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    h ^= 0xFFU; h *= 1099511628211ULL;
  };
  mix(tx); mix(std::to_string(version));
  for (const auto& e : events) { mix(e.event_id); mix(e.kind); mix(e.payload); }
  std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << h;
  return out.str();
}
std::string begin_frame(const std::string& tx, const std::string& idem,
                        std::uint64_t expected, const std::string& fingerprint) {
  std::ostringstream out;
  out << "B " << std::quoted(tx) << ' ' << std::quoted(idem) << ' '
      << expected << ' ' << std::quoted(fingerprint);
  return out.str();
}
std::string event_frame(const std::string& tx, const JournalEvent& e) {
  std::ostringstream out;
  out << "E " << std::quoted(tx) << ' ' << std::quoted(e.event_id) << ' '
      << std::quoted(e.kind) << ' ' << std::quoted(e.payload);
  return out.str();
}
std::string commit_frame(const std::string& tx, std::uint64_t version,
                         std::size_t count, const std::string& fingerprint) {
  std::ostringstream out;
  out << "C " << std::quoted(tx) << ' ' << version << ' ' << count << ' '
      << std::quoted(fingerprint);
  return out.str();
}
}  // namespace

EventJournal::EventJournal(std::string path) : path_(std::move(path)) {}

bool EventJournal::open() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (opened_) return true;
  std::error_code ec;
  const auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  if (ec) { last_error_ = "JOURNAL_DIRECTORY_ERROR"; return false; }
  if (!std::filesystem::exists(path_)) {
    std::ofstream create(path_, std::ios::binary | std::ios::out);
    if (!create) { last_error_ = "JOURNAL_CREATE_ERROR"; return false; }
    create.write(kMagic, static_cast<std::streamsize>(sizeof(kMagic) - 1));
    create.flush();
    if (!create) { last_error_ = "JOURNAL_HEADER_WRITE_ERROR"; return false; }
  }
  if (!recover()) return false;
  opened_ = true;
  return true;
}

bool EventJournal::append_frame(const std::string& payload) {
  if (payload.size() > kMaxFrame) { last_error_ = "JOURNAL_FRAME_TOO_LARGE"; return false; }
  std::ofstream out(path_, std::ios::binary | std::ios::app);
  if (!out) { last_error_ = "JOURNAL_OPEN_APPEND_ERROR"; return false; }
  put_u32(out, static_cast<std::uint32_t>(payload.size()));
  out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  put_u32(out, crc32(payload));
  out.flush();
  if (!out) { last_error_ = "JOURNAL_WRITE_ERROR"; return false; }
  return true;
}

bool EventJournal::recover() {
  std::ifstream in(path_, std::ios::binary);
  if (!in) { last_error_ = "JOURNAL_OPEN_READ_ERROR"; return false; }
  std::array<char, sizeof(kMagic) - 1> magic{};
  in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (!in || std::string(magic.data(), magic.size()) != std::string(kMagic, sizeof(kMagic)-1)) {
    last_error_ = "JOURNAL_HEADER_INVALID"; return false;
  }
  struct Pending {
    std::string idem, fingerprint;
    std::uint64_t expected{};
    std::vector<JournalEvent> events;
  };
  std::map<std::string, Pending> pending;
  for (;;) {
    std::uint32_t length = 0;
    const auto frame_start = in.tellg();
    if (!get_u32(in, length)) {
      if (in.eof()) break;
      last_error_ = "JOURNAL_LENGTH_READ_ERROR"; return false;
    }
    if (length > kMaxFrame) { last_error_ = "JOURNAL_FRAME_LENGTH_INVALID"; return false; }
    std::string payload(length, '\0');
    in.read(payload.data(), static_cast<std::streamsize>(length));
    if (!in) { in.clear(); break; }  // incomplete tail: transaction cannot be committed
    std::uint32_t stored_crc = 0;
    if (!get_u32(in, stored_crc)) { in.clear(); break; }
    if (stored_crc != crc32(payload)) {
      last_error_ = "JOURNAL_CHECKSUM_MISMATCH@" + std::to_string(static_cast<long long>(frame_start));
      return false;
    }
    std::istringstream frame(payload);
    char type = 0; frame >> type;
    if (type == 'B') {
      std::string tx, idem, fingerprint; std::uint64_t expected = 0;
      if (!(frame >> std::quoted(tx) >> std::quoted(idem) >> expected >> std::quoted(fingerprint)) ||
          tx.empty() || idem.empty() || fingerprint.empty() || pending.count(tx)) {
        last_error_ = "JOURNAL_BEGIN_RECORD_INVALID"; return false;
      }
      pending.emplace(tx, Pending{idem, fingerprint, expected, {}});
    } else if (type == 'E') {
      std::string tx; JournalEvent event;
      if (!(frame >> std::quoted(tx) >> std::quoted(event.event_id) >>
            std::quoted(event.kind) >> std::quoted(event.payload)) ||
          !pending.count(tx) || event.event_id.empty() || event.kind.empty()) {
        last_error_ = "JOURNAL_EVENT_RECORD_INVALID"; return false;
      }
      pending.at(tx).events.push_back(std::move(event));
    } else if (type == 'C') {
      std::string tx, fingerprint; std::uint64_t version = 0; std::size_t count = 0;
      if (!(frame >> std::quoted(tx) >> version >> count >> std::quoted(fingerprint)) ||
          !pending.count(tx)) {
        last_error_ = "JOURNAL_COMMIT_RECORD_INVALID"; return false;
      }
      auto p = pending.at(tx);
      if (p.expected != state_version_ || version != state_version_ + 1 ||
          p.events.size() != count || p.fingerprint != fingerprint ||
          request_fingerprint(tx, p.expected, p.events) != p.fingerprint ||
          idempotency_.count(p.idem) || transaction_ids_.count(tx)) {
        last_error_ = "JOURNAL_COMMIT_INVARIANT_FAILED"; return false;
      }
      for (const auto& event : p.events) {
        if (event_ids_.count(event.event_id)) { last_error_ = "JOURNAL_EVENT_ID_DUPLICATE"; return false; }
      }
      for (const auto& event : p.events) { events_.push_back(event); event_ids_[event.event_id] = true; }
      idempotency_[p.idem] = p.fingerprint;
      transaction_ids_[tx] = p.fingerprint;
      state_version_ = version;
      pending.erase(tx);
    } else {
      last_error_ = "JOURNAL_RECORD_TYPE_INVALID"; return false;
    }
  }
  return true;
}

JournalCommitResult EventJournal::commit(const std::string& tx, const std::string& idem,
                                          std::uint64_t expected,
                                          const std::vector<JournalEvent>& events) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!opened_) return {false, false, state_version_, "JOURNAL_NOT_OPEN"};
  if (tx.empty() || idem.empty() || events.empty()) return {false, false, state_version_, "TRANSACTION_INVALID"};
  const auto fingerprint = request_fingerprint(tx, expected, events);
  auto old = idempotency_.find(idem);
  if (old != idempotency_.end()) {
    if (old->second == fingerprint) return {true, true, state_version_, {}};
    return {false, false, state_version_, "IDEMPOTENCY_KEY_REUSED"};
  }
  if (transaction_ids_.count(tx)) return {false, false, state_version_, "TRANSACTION_ID_REUSED"};
  if (expected != state_version_) return {false, false, state_version_, "STATE_VERSION_CONFLICT"};
  std::map<std::string, bool> batch_ids;
  for (const auto& event : events) {
    if (event.event_id.empty() || event.kind.empty() || event_ids_.count(event.event_id) ||
        batch_ids.count(event.event_id)) return {false, false, state_version_, "EVENT_ID_INVALID_OR_DUPLICATE"};
    batch_ids[event.event_id] = true;
  }
  if (!append_frame(begin_frame(tx, idem, expected, fingerprint)))
    return {false, false, state_version_, last_error_};
  for (const auto& event : events)
    if (!append_frame(event_frame(tx, event))) return {false, false, state_version_, last_error_};
  if (!append_frame(commit_frame(tx, expected + 1, events.size(), fingerprint)))
    return {false, false, state_version_, last_error_};
  for (const auto& event : events) { events_.push_back(event); event_ids_[event.event_id] = true; }
  idempotency_[idem] = fingerprint;
  transaction_ids_[tx] = fingerprint;
  ++state_version_;
  return {true, false, state_version_, {}};
}
std::uint64_t EventJournal::state_version() const {
  std::lock_guard<std::mutex> lock(mutex_); return state_version_;
}
std::vector<JournalEvent> EventJournal::committed_events() const {
  std::lock_guard<std::mutex> lock(mutex_); return events_;
}
std::string EventJournal::last_error() const {
  std::lock_guard<std::mutex> lock(mutex_); return last_error_;
}
}  // namespace surge
