#include "surge/language_runtime.hpp"
#include <algorithm>
#include <set>

namespace surge {

LanguageRuntimeRegistry::LanguageRuntimeRegistry(LanguageRuntimeLimits limits)
    : limits_(limits) {}

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
  if (manifest.language_set_id.empty() || manifest.version.empty() ||
      manifest.digest.empty() || manifest.locale.empty() || manifest.script.empty() ||
      manifest.package_bytes == 0 || manifest.unpacked_bytes == 0 ||
      manifest.resident_bytes == 0 || manifest.max_input_bytes == 0 ||
      manifest.max_candidates == 0) {
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
  installed_.emplace(id, manifest);
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
  session_pins_[session_id] = set;
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
  return session_pins_.erase(session_id) != 0;
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
