#include "surge/snapshot_store.hpp"

#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace surge {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t kIoChunk = 64U * 1024U;

bool fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}

fs::path native_path(const std::string& value) {
  return fs::u8path(value);
}

bool exists_regular_file(const fs::path& path, bool& exists,
                         std::string* error) {
  std::error_code ec;
  const auto status = fs::status(path, ec);
  if (ec == std::errc::no_such_file_or_directory) {
    exists = false;
    return true;
  }
  if (ec) return fail(error, "SNAPSHOT_STORE_STAT_FAILED");
  exists = fs::is_regular_file(status);
  if (fs::exists(status) && !exists)
    return fail(error, "SNAPSHOT_STORE_NOT_REGULAR_FILE");
  return true;
}

void remove_if_present(const fs::path& path) {
  std::error_code ignored;
  fs::remove(path, ignored);
}

bool write_durable_file(const fs::path& path, const std::string& bytes,
                        std::string* error) {
#ifdef _WIN32
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return fail(error, "SNAPSHOT_STORE_TEMP_OPEN_FAILED");
  std::size_t offset = 0;
  bool ok = true;
  while (offset < bytes.size()) {
    const auto remaining = bytes.size() - offset;
    const DWORD amount = static_cast<DWORD>(
        remaining > static_cast<std::size_t>(MAXDWORD) ? MAXDWORD : remaining);
    DWORD written = 0;
    if (!WriteFile(file, bytes.data() + offset, amount, &written, nullptr) ||
        written == 0) { ok = false; break; }
    offset += written;
  }
  if (ok && !FlushFileBuffers(file)) ok = false;
  if (!CloseHandle(file)) ok = false;
  if (!ok) return fail(error, "SNAPSHOT_STORE_TEMP_WRITE_OR_SYNC_FAILED");
#else
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return fail(error, "SNAPSHOT_STORE_TEMP_OPEN_FAILED");
  std::size_t offset = 0;
  bool ok = true;
  while (offset < bytes.size()) {
    const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) { ok = false; break; }
    offset += static_cast<std::size_t>(written);
  }
  if (ok && ::fsync(fd) != 0) ok = false;
  if (::close(fd) != 0) ok = false;
  if (!ok) return fail(error, "SNAPSHOT_STORE_TEMP_WRITE_OR_SYNC_FAILED");
#endif
  if (error) error->clear();
  return true;
}

bool copy_durable_file(const fs::path& source, const fs::path& destination,
                       std::size_t maximum_bytes, std::string* error) {
#ifdef _WIN32
  HANDLE input = CreateFileW(source.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (input == INVALID_HANDLE_VALUE)
    return fail(error, "SNAPSHOT_STORE_BACKUP_SOURCE_OPEN_FAILED");
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(input, &size) || size.QuadPart < 0 ||
      static_cast<unsigned long long>(size.QuadPart) > maximum_bytes) {
    CloseHandle(input);
    return fail(error, "SNAPSHOT_STORE_BACKUP_SOURCE_LIMIT_EXCEEDED");
  }
  HANDLE output = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) {
    CloseHandle(input);
    return fail(error, "SNAPSHOT_STORE_BACKUP_TEMP_OPEN_FAILED");
  }
  std::array<char, kIoChunk> buffer{};
  bool ok = true;
  for (;;) {
    DWORD read_count = 0;
    if (!ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read_count, nullptr)) { ok = false; break; }
    if (read_count == 0) break;
    DWORD offset = 0;
    while (offset < read_count) {
      DWORD written = 0;
      if (!WriteFile(output, buffer.data() + offset, read_count - offset,
                     &written, nullptr) || written == 0) {
        ok = false; break;
      }
      offset += written;
    }
    if (!ok) break;
  }
  if (ok && !FlushFileBuffers(output)) ok = false;
  if (!CloseHandle(output)) ok = false;
  if (!CloseHandle(input)) ok = false;
  if (!ok) return fail(error, "SNAPSHOT_STORE_BACKUP_COPY_FAILED");
#else
  const int input = ::open(source.c_str(), O_RDONLY);
  if (input < 0) return fail(error, "SNAPSHOT_STORE_BACKUP_SOURCE_OPEN_FAILED");
  struct stat info {};
  if (::fstat(input, &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_size < 0 ||
      static_cast<unsigned long long>(info.st_size) > maximum_bytes) {
    ::close(input);
    return fail(error, "SNAPSHOT_STORE_BACKUP_SOURCE_LIMIT_EXCEEDED");
  }
  const int output = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (output < 0) {
    ::close(input);
    return fail(error, "SNAPSHOT_STORE_BACKUP_TEMP_OPEN_FAILED");
  }
  std::array<char, kIoChunk> buffer{};
  bool ok = true;
  for (;;) {
    const auto count = ::read(input, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) { ok = false; break; }
    if (count == 0) break;
    ssize_t offset = 0;
    while (offset < count) {
      const auto written = ::write(output, buffer.data() + offset,
                                   static_cast<std::size_t>(count - offset));
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) { ok = false; break; }
      offset += written;
    }
    if (!ok) break;
  }
  if (ok && ::fsync(output) != 0) ok = false;
  if (::close(output) != 0) ok = false;
  if (::close(input) != 0) ok = false;
  if (!ok) return fail(error, "SNAPSHOT_STORE_BACKUP_COPY_FAILED");
#endif
  if (error) error->clear();
  return true;
}

bool atomic_replace(const fs::path& source, const fs::path& destination,
                    std::string* error) {
#ifdef _WIN32
  if (!MoveFileExW(source.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return fail(error, "SNAPSHOT_STORE_ATOMIC_REPLACE_FAILED");
#else
  if (::rename(source.c_str(), destination.c_str()) != 0)
    return fail(error, "SNAPSHOT_STORE_ATOMIC_REPLACE_FAILED");
#endif
  if (error) error->clear();
  return true;
}

bool sync_parent_directory(const fs::path& path, std::string* error) {
#ifdef _WIN32
  // MOVEFILE_WRITE_THROUGH supplies the strongest supported replacement
  // durability barrier on Windows; directory handles cannot be flushed
  // portably through the Win32 API.
  (void)path;
  if (error) error->clear();
  return true;
#else
  auto parent = path.parent_path();
  if (parent.empty()) parent = ".";
  const int fd = ::open(parent.c_str(), O_RDONLY
#ifdef O_DIRECTORY
      | O_DIRECTORY
#endif
  );
  if (fd < 0) return fail(error, "SNAPSHOT_STORE_DIRECTORY_OPEN_FAILED");
  const bool ok = ::fsync(fd) == 0;
  const bool closed = ::close(fd) == 0;
  if (!ok || !closed)
    return fail(error, "SNAPSHOT_STORE_DIRECTORY_SYNC_FAILED");
  if (error) error->clear();
  return true;
#endif
}

bool read_bounded_file(const fs::path& path, std::size_t maximum_bytes,
                       std::string& bytes, std::string* error) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec) || ec)
    return fail(error, "SNAPSHOT_STORE_FILE_NOT_FOUND_OR_NOT_REGULAR");
  const auto size = fs::file_size(path, ec);
  if (ec) return fail(error, "SNAPSHOT_STORE_FILE_SIZE_FAILED");
  if (size > maximum_bytes)
    return fail(error, "SNAPSHOT_STORE_FILE_LIMIT_EXCEEDED");
  if (size > static_cast<std::uintmax_t>(
          std::numeric_limits<std::size_t>::max()))
    return fail(error, "SNAPSHOT_STORE_FILE_SIZE_INVALID");
  std::ifstream input(path, std::ios::binary);
  if (!input) return fail(error, "SNAPSHOT_STORE_FILE_OPEN_FAILED");
  std::string candidate(static_cast<std::size_t>(size), '\0');
  if (size != 0) {
    input.read(candidate.data(), static_cast<std::streamsize>(size));
    if (!input || static_cast<std::uintmax_t>(input.gcount()) != size)
      return fail(error, "SNAPSHOT_STORE_FILE_READ_FAILED");
  }
  char trailing = 0;
  if (input.read(&trailing, 1))
    return fail(error, "SNAPSHOT_STORE_FILE_CHANGED_DURING_READ");
  bytes.swap(candidate);
  if (error) error->clear();
  return true;
}

SnapshotStoreResult load_generation(const fs::path& path,
                                    std::size_t maximum_bytes,
                                    bool backup) {
  SnapshotStoreResult result;
  if (!read_bounded_file(path, maximum_bytes, result.bytes, &result.error))
    return result;
  SnapshotLimits limits;
  limits.max_total_bytes = maximum_bytes;
  if (!SnapshotCodec::decode(result.bytes, limits, &result.document,
                             &result.error)) {
    result.bytes.clear();
    result.error = std::string("SNAPSHOT_STORE_GENERATION_INVALID:") + result.error;
    return result;
  }
  result.ok = true;
  result.used_backup = backup;
  result.error.clear();
  return result;
}
}  // namespace

SnapshotStore::SnapshotStore(std::string path, SnapshotStoreLimits limits)
    : path_(std::move(path)), limits_(limits) {}

bool SnapshotStore::save(const std::string& bytes, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (path_.empty()) return fail(error, "SNAPSHOT_STORE_PATH_REQUIRED");
  if (limits_.max_file_bytes == 0 || bytes.size() > limits_.max_file_bytes)
    return fail(error, "SNAPSHOT_STORE_FILE_LIMIT_EXCEEDED");

  SnapshotDocument document;
  SnapshotLimits codec_limits;
  codec_limits.max_total_bytes = limits_.max_file_bytes;
  if (!SnapshotCodec::decode(bytes, codec_limits, &document, error))
    return false;

  const auto current = native_path(path_);
  const fs::path temporary(path_ + ".tmp");
  const fs::path backup(path_ + ".bak");
  const fs::path backup_temporary(path_ + ".bak.tmp");
  remove_if_present(temporary);
  remove_if_present(backup_temporary);

  if (!write_durable_file(temporary, bytes, error)) {
    remove_if_present(temporary);
    return false;
  }

  bool current_exists = false;
  if (!exists_regular_file(current, current_exists, error)) {
    remove_if_present(temporary);
    return false;
  }
  if (limits_.keep_backup && current_exists) {
    if (!copy_durable_file(current, backup_temporary,
                           limits_.max_file_bytes, error)) {
      remove_if_present(temporary);
      remove_if_present(backup_temporary);
      return false;
    }
    if (!atomic_replace(backup_temporary, backup, error) ||
        !sync_parent_directory(current, error)) {
      remove_if_present(temporary);
      remove_if_present(backup_temporary);
      return false;
    }
  }

  if (!atomic_replace(temporary, current, error)) {
    remove_if_present(temporary);
    return false;
  }
  if (!sync_parent_directory(current, error))
    return false;  // Replacement may have succeeded; caller must load to resolve.
  if (error) error->clear();
  return true;
}

SnapshotStoreResult SnapshotStore::load() const {
  std::lock_guard<std::mutex> lock(mutex_);
  SnapshotStoreResult primary;
  if (path_.empty()) {
    primary.error = "SNAPSHOT_STORE_PATH_REQUIRED";
    return primary;
  }
  const auto current = native_path(path_);
  primary = load_generation(current, limits_.max_file_bytes, false);
  if (primary.ok) return primary;

  const auto backup = native_path(path_ + ".bak");
  auto previous = load_generation(backup, limits_.max_file_bytes, true);
  if (previous.ok) return previous;

  SnapshotStoreResult failure;
  if (primary.error == "SNAPSHOT_STORE_FILE_NOT_FOUND_OR_NOT_REGULAR" &&
      previous.error == "SNAPSHOT_STORE_FILE_NOT_FOUND_OR_NOT_REGULAR") {
    failure.error = "SNAPSHOT_STORE_NOT_FOUND";
  } else {
    failure.error = "SNAPSHOT_STORE_NO_VALID_GENERATION:primary=" +
        primary.error + ";backup=" + previous.error;
  }
  return failure;
}

}  // namespace surge
