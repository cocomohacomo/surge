#include "surge/language_runtime.hpp"
#include "surge/sha256.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <algorithm>
#include <cctype>
#include <set>

namespace surge {
namespace {
bool is_sha256_hex(const std::string& value) {
  return value.size() == 64 && std::all_of(value.begin(), value.end(),
      [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

bool is_safe_relative_resource_path(const std::string& value) {
  if (value.empty() || value.size() > 512 || value.front() == '/' ||
      value.find('\\\\') != std::string::npos ||
      value.find(':') != std::string::npos || value.find('\\0') != std::string::npos)
    return false;
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const auto end = value.find('/', begin);
    const auto part = value.substr(begin, end == std::string::npos
        ? std::string::npos : end - begin);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return true;
}

bool path_is_within(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) {
  auto r = root.begin();
  auto c = candidate.begin();
  for (; r != root.end(); ++r, ++c) {
    if (c == candidate.end() || *r != *c) return false;
  }
  return true;
}

bool verify_language_package_directory(const std::string& root_text,
    const LanguageSetManifest& manifest, const LanguageRuntimeLimits& limits,
    std::string& error) {
  error.clear();
  namespace fs = std::filesystem;
  if (root_text.empty() || manifest.resources.empty() ||
      manifest.resources.size() > limits.max_resources ||
      manifest.package_bytes == 0 ||
      manifest.package_bytes > limits.max_package_bytes ||
      manifest.unpacked_bytes == 0 ||
      manifest.unpacked_bytes > limits.max_total_unpacked_bytes) {
    error = "LANGUAGE_PACKAGE_LIMIT_OR_MANIFEST_INVALID";
    return false;
  }
  std::error_code ec;
  const auto root = fs::canonical(fs::path(root_text), ec);
  if (ec || !fs::is_directory(root, ec) || ec) {
    error = "LANGUAGE_PACKAGE_ROOT_INVALID";
    return false;
  }
  std::set<std::string> seen;
  std::size_t total = 0;
  constexpr std::size_t kChunkBytes = 64U * 1024U;
  for (const auto& resource : manifest.resources) {
    if (!is_safe_relative_resource_path(resource.path) ||
        !is_sha256_hex(resource.sha256) ||
        resource.kind.empty() || resource.kind.size() > 32 ||
        resource.kind == "native_module" ||
        (resource.kind != "lexicon" && resource.kind != "morphology" &&
         resource.kind != "connection" && resource.kind != "ngram" &&
         resource.kind != "orthography" && resource.kind != "rules" &&
         resource.kind != "model" && resource.kind != "test_vectors" &&
         resource.kind != "metadata") ||
        !seen.insert(resource.path).second) {
      error = "LANGUAGE_PACKAGE_RESOURCE_DESCRIPTOR_INVALID";
      return false;
    }
    if (resource.bytes > limits.max_total_unpacked_bytes ||
        total > limits.max_total_unpacked_bytes - resource.bytes) {
      error = "LANGUAGE_PACKAGE_UNPACKED_LIMIT_EXCEEDED";
      return false;
    }
    const fs::path relative(resource.path);
    auto current = root;
    for (const auto& component : relative) {
      current /= component;
      const auto status = fs::symlink_status(current, ec);
      if (ec || fs::is_symlink(status)) {
        error = "LANGUAGE_PACKAGE_SYMLINK_REJECTED";
        return false;
      }
    }
    const auto resolved = fs::canonical(root / relative, ec);
    if (ec || !path_is_within(root, resolved) ||
        !fs::is_regular_file(resolved, ec) || ec) {
      error = "LANGUAGE_PACKAGE_RESOURCE_PATH_INVALID";
      return false;
    }
    const auto actual_size = fs::file_size(resolved, ec);
    if (ec || actual_size != resource.bytes) {
      error = "LANGUAGE_PACKAGE_RESOURCE_SIZE_MISMATCH";
      return false;
    }
    std::ifstream input(resolved, std::ios::binary);
    if (!input) {
      error = "LANGUAGE_PACKAGE_RESOURCE_OPEN_FAILED";
      return false;
    }
    Sha256Hasher hasher;
    std::array<char, kChunkBytes> buffer{};
    std::size_t read_total = 0;
    while (input) {
      input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      const auto count = static_cast<std::size_t>(input.gcount());
      if (count != 0) {
        if (count > resource.bytes - read_total ||
            !hasher.update(buffer.data(), count)) {
          error = "LANGUAGE_PACKAGE_RESOURCE_READ_INVALID";
          return false;
        }
        read_total += count;
      }
    }
    if (!input.eof() || read_total != resource.bytes) {
      error = "LANGUAGE_PACKAGE_RESOURCE_READ_FAILED";
      return false;
    }
    auto actual_digest = hasher.finalize_hex();
    auto expected_digest = resource.sha256;
    std::transform(expected_digest.begin(), expected_digest.end(),
        expected_digest.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
        });
    if (actual_digest != expected_digest) {
      error = "LANGUAGE_PACKAGE_RESOURCE_DIGEST_MISMATCH";
      return false;
    }
    total += resource.bytes;
  }
  if (total != manifest.unpacked_bytes) {
    error = "LANGUAGE_PACKAGE_UNPACKED_SIZE_MISMATCH";
    return false;
  }
  return true;
}

}  // namespace

LanguageRuntimeRegistry::LanguageRuntimeRegistry(LanguageRuntimeLimits limits)
    : limits_(limits) {}

LanguageRuntimeRegistry::LanguageRuntimeRegistry(ResourceBudget& budget,
    LanguageRuntimeLimits limits)
    : limits_(limits), budget_(&budget) {}

std::string LanguageRuntimeRegistry::key(const std::string& id,
                                         const std::string& version) {
  return std::to_string(id.size()) + ":" + id +
         std::to_string(version.size()) + ":" + version;
}

bool LanguageRuntimeRegistry::add_within(std::size_t& total,
    std::size_t value, std::size_t limit) {
  if (value > limit || total > limit - value) return false;
  total += value;
  return true;
}

bool LanguageRuntimeRegistry::install_manifest(
    const LanguageSetManifest& manifest, std::string& error) {
  error.clear();
  if (manifest.language_set_id.empty() || manifest.language_set_id.size() > 128 ||
      manifest.version.empty() || manifest.version.size() > 64 ||
      !is_sha256_hex(manifest.digest) ||
      manifest.locale.empty() || manifest.locale.size() > 64 ||
      manifest.script.empty() || manifest.script.size() > 64 ||
      manifest.package_bytes == 0 || manifest.unpacked_bytes == 0 ||
      manifest.resident_bytes == 0 || manifest.max_input_bytes == 0 ||
      manifest.max_input_bytes > limits_.max_input_bytes ||
      manifest.max_candidates == 0 || manifest.max_candidates > limits_.max_candidates ||
      manifest.capabilities.size() > limits_.max_capabilities) {
    error = "LANGUAGE_SET_MANIFEST_INVALID";
    return false;
  }
  if (manifest.package_bytes > limits_.max_package_bytes ||
      manifest.scratch_bytes > limits_.max_scratch_bytes) {
    error = "LANGUAGE_SET_RESOURCE_LIMIT_EXCEEDED";
    return false;
  }
  std::set<std::string> capabilities;
  for (const auto& capability : manifest.capabilities) {
    if (capability.empty() || capability.size() > 128 ||
        !capabilities.insert(capability).second) {
      error = "LANGUAGE_SET_CAPABILITY_INVALID";
      return false;
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto id = key(manifest.language_set_id, manifest.version);
  if (installed_.count(id)) {
    error = "LANGUAGE_SET_ALREADY_INSTALLED";
    return false;
  }
  if (installed_.size() >= limits_.max_installed_sets) {
    error = "LANGUAGE_SET_COUNT_BUDGET_EXCEEDED";
    return false;
  }
  std::size_t packages = 0, unpacked = 0, resident = 0;
  for (const auto& item : installed_) {
    if (!add_within(packages, item.second.package_bytes, limits_.max_package_bytes) ||
        !add_within(unpacked, item.second.unpacked_bytes, limits_.max_total_unpacked_bytes) ||
        !add_within(resident, item.second.resident_bytes, limits_.max_total_resident_bytes)) {
      error = "LANGUAGE_SET_RESOURCE_ACCOUNTING_INVALID";
      return false;
    }
  }
  if (!add_within(packages, manifest.package_bytes, limits_.max_package_bytes) ||
      !add_within(unpacked, manifest.unpacked_bytes, limits_.max_total_unpacked_bytes) ||
      !add_within(resident, manifest.resident_bytes, limits_.max_total_resident_bytes)) {
    error = "LANGUAGE_SET_RESOURCE_BUDGET_EXCEEDED";
    return false;
  }
  ResourceLease resident_lease;
  if (budget_ && !budget_->try_acquire(ResourceKind::ResidentBytes,
      manifest.resident_bytes, resident_lease, error)) {
    error = "LANGUAGE_SET_RESIDENT_" + error;
    return false;
  }
  installed_.emplace(id, manifest);
  if (budget_) resident_leases_.emplace(id, std::move(resident_lease));
  return true;
}

bool LanguageRuntimeRegistry::activate_at_boundary(
    const std::string& session_id, const std::string& language_set_id,
    const std::string& version, bool session_boundary, std::string& error) {
  error.clear();
  if (session_id.empty() || language_set_id.empty() || version.empty()) {
    error = "LANGUAGE_SESSION_ARGUMENT_INVALID";
    return false;
  }
  if (!session_boundary) {
    error = "LANGUAGE_SWITCH_REQUIRES_SESSION_BOUNDARY";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto set = key(language_set_id, version);
  if (!installed_.count(set)) {
    error = "LANGUAGE_SET_NOT_INSTALLED";
    return false;
  }
  if (!session_pins_.count(session_id) && session_pins_.size() >= limits_.max_sessions) {
    error = "LANGUAGE_SESSION_BUDGET_EXCEEDED";
    return false;
  }
  const auto current = session_pins_.find(session_id);
  if (current != session_pins_.end() && current->second == set) return true;
  ResourceLease scratch_lease;
  if (budget_ && !budget_->try_acquire(ResourceKind::TransientBytes,
      installed_.at(set).scratch_bytes, scratch_lease, error)) {
    error = "LANGUAGE_SET_SCRATCH_" + error;
    return false;
  }
  session_pins_[session_id] = set;
  if (budget_) session_scratch_leases_[session_id] = std::move(scratch_lease);
  return true;
}

bool LanguageRuntimeRegistry::active_manifest(
    const std::string& session_id, LanguageSetManifest& manifest,
    std::string& error) const {
  error.clear();
  std::lock_guard<std::mutex> lock(mutex_);
  const auto pin = session_pins_.find(session_id);
  if (pin == session_pins_.end()) {
    error = "LANGUAGE_SESSION_NOT_PINNED";
    return false;
  }
  const auto set = installed_.find(pin->second);
  if (set == installed_.end()) {
    error = "LANGUAGE_SESSION_PIN_INVALID";
    return false;
  }
  manifest = set->second;
  return true;
}

bool LanguageRuntimeRegistry::release_session(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto erased = session_pins_.erase(session_id) != 0;
  session_scratch_leases_.erase(session_id);
  return erased;
}

bool LanguageRuntimeRegistry::uninstall(
    const std::string& language_set_id, const std::string& version,
    std::string& error) {
  error.clear();
  std::lock_guard<std::mutex> lock(mutex_);
  const auto id = key(language_set_id, version);
  for (const auto& pin : session_pins_) {
    if (pin.second == id) {
      error = "LANGUAGE_SET_PINNED_BY_SESSION";
      return false;
    }
  }
  if (installed_.erase(id) == 0) {
    error = "LANGUAGE_SET_NOT_INSTALLED";
    return false;
  }
  resident_leases_.erase(id);
  return true;
}

std::size_t LanguageRuntimeRegistry::installed_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return installed_.size();
}

std::size_t LanguageRuntimeRegistry::pinned_session_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return session_pins_.size();
}

}  // namespace surge
