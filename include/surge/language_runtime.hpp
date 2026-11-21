#pragma once
#include "surge/resource_budget.hpp"
#include <cstdint>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace surge {

struct LanguageSetResource {
  std::string path;
  std::string kind;
  std::string sha256;
  std::size_t bytes{0};
  bool required{true};
};

struct LanguageSetManifest {
  std::string language_set_id;
  std::string version;
  std::string digest;
  std::string locale;
  std::string script;
  std::size_t package_bytes{0};
  std::size_t unpacked_bytes{0};
  std::size_t resident_bytes{0};
  std::size_t scratch_bytes{0};
  std::size_t max_input_bytes{0};
  std::size_t max_candidates{0};
  std::vector<std::string> capabilities;
  std::vector<LanguageSetResource> resources;
};

struct LanguageRuntimeLimits {
  std::size_t max_installed_sets{8};
  std::size_t max_sessions{256};
  std::size_t max_package_bytes{64U * 1024U * 1024U};
  std::size_t max_total_unpacked_bytes{256U * 1024U * 1024U};
  std::size_t max_total_resident_bytes{128U * 1024U * 1024U};
  std::size_t max_scratch_bytes{32U * 1024U * 1024U};
  std::size_t max_input_bytes{16U * 1024U * 1024U};
  std::size_t max_candidates{4096};
  std::size_t max_capabilities{64};
  std::size_t max_resources{256};
};

// Verifies a directory-backed, data-only pack before metadata installation.
// Each declared resource is streamed through SHA-256; paths must remain inside
// the canonical root and native modules are rejected.
bool verify_language_package_directory(const std::string& root,
    const LanguageSetManifest& manifest, const LanguageRuntimeLimits& limits,
    std::string& error);

class LanguageRuntimeRegistry {
 public:
  explicit LanguageRuntimeRegistry(LanguageRuntimeLimits limits = {});
  LanguageRuntimeRegistry(ResourceBudget& budget, LanguageRuntimeLimits limits = {});
  bool install_manifest(const LanguageSetManifest& manifest, std::string& error);
  bool install_verified_package(const std::string& root,
      const LanguageSetManifest& manifest, std::string& error);
  bool activate_at_boundary(const std::string& session_id,
      const std::string& language_set_id, const std::string& version,
      bool session_boundary, std::string& error);
  bool active_manifest(const std::string& session_id,
      LanguageSetManifest& manifest, std::string& error) const;
  bool release_session(const std::string& session_id);
  bool uninstall(const std::string& language_set_id,
      const std::string& version, std::string& error);
  std::size_t installed_count() const;
  std::size_t pinned_session_count() const;

 private:
  static std::string key(const std::string& id, const std::string& version);
  static bool add_within(std::size_t& total, std::size_t value, std::size_t limit);
  LanguageRuntimeLimits limits_;
  mutable std::mutex mutex_;
  std::map<std::string, LanguageSetManifest> installed_;
  ResourceBudget* budget_{nullptr};
  std::map<std::string, ResourceLease> resident_leases_;
  std::map<std::string, ResourceLease> session_scratch_leases_;
  std::map<std::string, std::string> session_pins_;
};

}  // namespace surge
