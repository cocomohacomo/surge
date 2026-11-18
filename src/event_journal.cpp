#include "surge/event_journal.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace surge {
namespace {
constexpr char kMagic[] = "SURGEJ1\n";
constexpr std::uint32_t kMaxFrame = 16U * 1024U * 1024U;

bool sync_file_to_stable_storage(const std::string& path) {
#ifdef _WIN32
  HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const BOOL ok = FlushFileBuffers(file);
  CloseHandle(file);
  return ok != 0;
#else
  const int fd = ::open(path.c_str(), O_WRONLY);
  if (fd < 0) return false;
  const int result = ::fsync(fd);
  ::close(fd);
  return result == 0;
#endif
}
bool sync_parent_directory(const std::string& path) {
#ifdef _WIN32
  (void)path;
  return true;
#else
  auto parent = std::filesystem::path(path).parent_path();
  if (parent.empty()) parent = ".";
  const int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) return false;
  const int result = ::fsync(fd);
  ::close(fd);
  return result == 0;
#endif
}

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

EventJournal::EventJournal(std::string path, EventJournalLimits limits)
    : path_(std::move(path)), limits_(limits) {}

EventJournal::EventJournal(std::string path, AppendFrameGate append_frame_gate)
    : path_(std::move(path)), append_frame_gate_(std::move(append_frame_gate)) {}

EventJournal::EventJournal(std::string path, AppendFrameGate append_frame_gate,
                           SyncFrameGate sync_frame_gate)
    : path_(std::move(path)), append_frame_gate_(std::move(append_frame_gate)),
      sync_frame_gate_(std::move(sync_frame_gate)) {}

EventJournal::~EventJournal() { release_writer_lock(); }

bool EventJournal::acquire_writer_lock() {
  const auto lock_path = path_ + ".lock";
#ifdef _WIN32
  HANDLE handle = CreateFileA(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
      0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    last_error_ = GetLastError() == ERROR_SHARING_VIOLATION
        ? "JOURNAL_WRITER_LOCKED" : "JOURNAL_WRITER_LOCK_ERROR";
    return false;
  }
  lock_handle_ = reinterpret_cast<std::intptr_t>(handle);
#else
  const int fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0600);
  if (fd < 0) { last_error_ = "JOURNAL_WRITER_LOCK_ERROR"; return false; }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd);
    last_error_ = "JOURNAL_WRITER_LOCKED";
    return false;
  }
  lock_handle_ = static_cast<std::intptr_t>(fd);
#endif
  return true;
}

void EventJournal::release_writer_lock() {
  if (lock_handle_ == -1) return;
#ifdef _WIN32
  CloseHandle(reinterpret_cast<HANDLE>(lock_handle_));
#else
  const int fd = static_cast<int>(lock_handle_);
  ::flock(fd, LOCK_UN);
  ::close(fd);
#endif
  lock_handle_ = -1;
}

bool EventJournal::open() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (opened_) return true;
  std::error_code ec;
  const auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  if (ec) { last_error_ = "JOURNAL_DIRECTORY_ERROR"; return false; }
  if (lock_handle_ == -1 && !acquire_writer_lock()) return false;
  if (!std::filesystem::exists(path_)) {
    std::ofstream create(path_, std::ios::binary | std::ios::out);
    if (!create) { last_error_ = "JOURNAL_CREATE_ERROR"; release_writer_lock(); return false; }
    create.write(kMagic, static_cast<std::streamsize>(sizeof(kMagic) - 1));
    create.flush();
    if (!create) { last_error_ = "JOURNAL_HEADER_WRITE_ERROR"; release_writer_lock(); return false; }
    create.close();
    if (!sync_file_to_stable_storage(path_) || !sync_parent_directory(path_)) {
      last_error_ = "JOURNAL_HEADER_SYNC_ERROR"; release_writer_lock(); return false;
    }
  }
  if (!recover()) { release_writer_lock(); return false; }
  opened_ = true;
  return true;
}

bool EventJournal::append_frame(const std::string& payload) {
  if (payload.size() > kMaxFrame) { last_error_ = "JOURNAL_FRAME_TOO_LARGE"; return false; }
  if (append_frame_gate_) {
    try {
      if (!append_frame_gate_(append_frame_count_)) {
        last_error_ = "JOURNAL_INJECTED_APPEND_FAILURE";
        return false;
      }
    } catch (...) {
      last_error_ = "JOURNAL_APPEND_GATE_EXCEPTION";
      return false;
    }
  }
  std::ofstream out(path_, std::ios::binary | std::ios::app);
  if (!out) { last_error_ = "JOURNAL_OPEN_APPEND_ERROR"; return false; }
  put_u32(out, static_cast<std::uint32_t>(payload.size()));
  out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  put_u32(out, crc32(payload));
  out.flush();
  if (!out) { last_error_ = "JOURNAL_WRITE_ERROR"; return false; }
  out.close();
  if (sync_frame_gate_) {
    try {
      if (!sync_frame_gate_(append_frame_count_)) {
        last_error_ = "JOURNAL_INJECTED_SYNC_FAILURE";
        return false;
      }
    } catch (...) {
      last_error_ = "JOURNAL_SYNC_GATE_EXCEPTION";
      return false;
    }
  }
  if (!sync_file_to_stable_storage(path_)) {
    last_error_ = "JOURNAL_SYNC_ERROR";
    return false;
  }
  ++append_frame_count_;
  return true;
}

bool EventJournal::recover() {
  state_version_ = 0;
  retained_payload_bytes_ = 0;
  events_.clear(); idempotency_.clear(); transaction_ids_.clear(); event_ids_.clear();
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
    std::streamoff begin_offset{};
  };
  std::map<std::string, Pending> pending;
  bool truncated_tail = false;
  std::streamoff truncate_at = 0;
  for (;;) {
    const auto frame_start = in.tellg();
    char length_bytes[4]{};
    in.read(length_bytes, 4);
    const auto length_read = in.gcount();
    if (length_read == 0 && in.eof()) break;
    if (length_read != 4) {
      truncated_tail = true; truncate_at = static_cast<std::streamoff>(frame_start); break;
    }
    const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(length_bytes[0]));
    const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(length_bytes[1]));
    const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(length_bytes[2]));
    const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(length_bytes[3]));
    const std::uint32_t length = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
    if (length > kMaxFrame) { last_error_ = "JOURNAL_FRAME_LENGTH_INVALID"; return false; }
    std::string payload(length, '\0');
    in.read(payload.data(), static_cast<std::streamsize>(length));
    if (in.gcount() != static_cast<std::streamsize>(length)) {
      truncated_tail = true; truncate_at = static_cast<std::streamoff>(frame_start); break;
    }
    char crc_bytes[4]{};
    in.read(crc_bytes, 4);
    if (in.gcount() != 4) {
      truncated_tail = true; truncate_at = static_cast<std::streamoff>(frame_start); break;
    }
    const auto c0 = static_cast<std::uint32_t>(static_cast<unsigned char>(crc_bytes[0]));
    const auto c1 = static_cast<std::uint32_t>(static_cast<unsigned char>(crc_bytes[1]));
    const auto c2 = static_cast<std::uint32_t>(static_cast<unsigned char>(crc_bytes[2]));
    const auto c3 = static_cast<std::uint32_t>(static_cast<unsigned char>(crc_bytes[3]));
    const std::uint32_t stored_crc = c0 | (c1 << 8U) | (c2 << 16U) | (c3 << 24U);
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
      pending.emplace(tx, Pending{idem, fingerprint, expected, {}, static_cast<std::streamoff>(frame_start)});
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
      std::size_t added_payload_bytes = 0;
      for (const auto& event : p.events) {
        if (event_ids_.count(event.event_id)) { last_error_ = "JOURNAL_EVENT_ID_DUPLICATE"; return false; }
        if (event.payload.size() > std::numeric_limits<std::size_t>::max() - added_payload_bytes) {
          last_error_ = "JOURNAL_RESOURCE_LIMIT_EXCEEDED"; return false;
        }
        added_payload_bytes += event.payload.size();
      }
      if ((limits_.max_events && p.events.size() > limits_.max_events -
              std::min(limits_.max_events, events_.size())) ||
          (limits_.max_payload_bytes && added_payload_bytes > limits_.max_payload_bytes -
              std::min(limits_.max_payload_bytes, retained_payload_bytes_)) ||
          (limits_.max_transactions && transaction_ids_.size() >= limits_.max_transactions)) {
        last_error_ = "JOURNAL_RESOURCE_LIMIT_EXCEEDED"; return false;
      }
      for (auto event : p.events) {
        event.state_version = version;
        events_.push_back(std::move(event));
        event_ids_[events_.back().event_id] = true;
      }
      retained_payload_bytes_ += added_payload_bytes;
      idempotency_[p.idem] = p.fingerprint;
      transaction_ids_[tx] = p.fingerprint;
      state_version_ = version;
      pending.erase(tx);
    } else {
      last_error_ = "JOURNAL_RECORD_TYPE_INVALID"; return false;
    }
  }
  if (!pending.empty()) {
    for (const auto& item : pending) {
      if (!truncated_tail || item.second.begin_offset < truncate_at) {
        truncated_tail = true;
        truncate_at = item.second.begin_offset;
      }
    }
  }
  in.close();
  if (truncated_tail) {
    std::error_code ec;
    std::filesystem::resize_file(path_, static_cast<std::uintmax_t>(truncate_at), ec);
    if (ec) { last_error_ = "JOURNAL_TAIL_TRUNCATE_ERROR"; return false; }
    if (!sync_file_to_stable_storage(path_)) {
      last_error_ = "JOURNAL_TAIL_SYNC_ERROR"; return false;
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
  if (state_version_ == std::numeric_limits<std::uint64_t>::max())
    return {false, false, state_version_, "STATE_VERSION_EXHAUSTED"};
  std::map<std::string, bool> batch_ids;
  std::size_t added_payload_bytes = 0;
  for (const auto& event : events) {
    if (event.event_id.empty() || event.kind.empty() || event_ids_.count(event.event_id) ||
        batch_ids.count(event.event_id)) return {false, false, state_version_, "EVENT_ID_INVALID_OR_DUPLICATE"};
    if (event.payload.size() > std::numeric_limits<std::size_t>::max() - added_payload_bytes)
      return {false, false, state_version_, "JOURNAL_RESOURCE_LIMIT_EXCEEDED"};
    added_payload_bytes += event.payload.size();
    batch_ids[event.event_id] = true;
  }
  if ((limits_.max_events && events.size() > limits_.max_events -
          std::min(limits_.max_events, events_.size())) ||
      (limits_.max_payload_bytes && added_payload_bytes > limits_.max_payload_bytes -
          std::min(limits_.max_payload_bytes, retained_payload_bytes_)) ||
      (limits_.max_transactions && transaction_ids_.size() >= limits_.max_transactions)) {
    return {false, false, state_version_, "JOURNAL_RESOURCE_LIMIT_EXCEEDED"};
  }
  if (!append_frame(begin_frame(tx, idem, expected, fingerprint))) {
    opened_ = false; return {false, false, state_version_, last_error_};
  }
  for (const auto& event : events) {
    if (!append_frame(event_frame(tx, event))) {
      opened_ = false; return {false, false, state_version_, last_error_};
    }
  }
  if (!append_frame(commit_frame(tx, expected + 1, events.size(), fingerprint))) {
    opened_ = false;
    if (last_error_ == "JOURNAL_INJECTED_SYNC_FAILURE" ||
        last_error_ == "JOURNAL_SYNC_GATE_EXCEPTION" ||
        last_error_ == "JOURNAL_SYNC_ERROR") {
      // The COMMIT frame may have reached the filesystem even though its
      // stable-storage barrier failed. Recovery plus idempotent retry resolves
      // whether the transaction committed.
      return {false, false, state_version_, "JOURNAL_COMMIT_OUTCOME_UNKNOWN"};
    }
    return {false, false, state_version_, last_error_};
  }
  const auto committed_version = state_version_ + 1;
  for (auto event : events) {
    event.state_version = committed_version;
    events_.push_back(std::move(event));
    event_ids_[events_.back().event_id] = true;
  }
  retained_payload_bytes_ += added_payload_bytes;
  idempotency_[idem] = fingerprint;
  transaction_ids_[tx] = fingerprint;
  state_version_ = committed_version;
  return {true, false, state_version_, {}};
}
std::uint64_t EventJournal::state_version() const {
  std::lock_guard<std::mutex> lock(mutex_); return state_version_;
}
std::vector<JournalEvent> EventJournal::committed_events() const {
  std::lock_guard<std::mutex> lock(mutex_); return events_;
}
JournalTailResult EventJournal::committed_tail(
    std::uint64_t after_state_version, JournalTailLimits limits) const {
  std::lock_guard<std::mutex> lock(mutex_);
  JournalTailResult result;
  result.after_state_version = after_state_version;
  result.current_state_version = state_version_;
  if (!opened_) {
    result.error = "JOURNAL_TAIL_NOT_OPEN";
    return result;
  }
  if (after_state_version > state_version_) {
    result.error = "JOURNAL_TAIL_WATERMARK_AHEAD";
    return result;
  }
  if (after_state_version == state_version_) {
    result.ok = true;
    return result;
  }
  if (limits.max_events == 0 || limits.max_total_bytes == 0) {
    result.error = "JOURNAL_TAIL_LIMIT_INVALID";
    return result;
  }

  std::uint64_t expected_version = after_state_version + 1;
  bool saw_first_transaction = false;
  for (const auto& event : events_) {
    if (event.state_version <= after_state_version) continue;
    if ((!saw_first_transaction && event.state_version != expected_version) ||
        (saw_first_transaction && event.state_version < expected_version) ||
        (saw_first_transaction && event.state_version > expected_version &&
         (expected_version == std::numeric_limits<std::uint64_t>::max() ||
          event.state_version != expected_version + 1))) {
      result.events.clear();
      result.total_bytes = 0;
      result.error = "JOURNAL_TAIL_VERSION_GAP";
      return result;
    }
    if (saw_first_transaction && event.state_version > expected_version)
      ++expected_version;
    saw_first_transaction = true;
    if (result.events.size() >= limits.max_events) {
      result.events.clear();
      result.total_bytes = 0;
      result.error = "JOURNAL_TAIL_EVENT_LIMIT_EXCEEDED";
      return result;
    }
    const auto remaining = limits.max_total_bytes -
        std::min(limits.max_total_bytes, result.total_bytes);
    if (event.event_id.size() > remaining ||
        event.kind.size() > remaining - std::min(remaining, event.event_id.size()) ||
        event.payload.size() > remaining - std::min(remaining, event.event_id.size() + event.kind.size())) {
      result.events.clear();
      result.total_bytes = 0;
      result.error = "JOURNAL_TAIL_TOTAL_BYTES_LIMIT_EXCEEDED";
      return result;
    }
    result.total_bytes += event.event_id.size() + event.kind.size() + event.payload.size();
    result.events.push_back(event);
  }
  if (expected_version != state_version_ || result.events.empty()) {
    result.events.clear();
    result.total_bytes = 0;
    result.error = "JOURNAL_TAIL_VERSION_GAP";
    return result;
  }
  result.ok = true;
  return result;
}
std::string EventJournal::last_error() const {
  std::lock_guard<std::mutex> lock(mutex_); return last_error_;
}
}  // namespace surge
