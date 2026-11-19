#pragma once
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace surge {

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
};

class LanguageRuntimeRegistry {
 public:
  explicit LanguageRuntimeRegistry(LanguageRuntimeLimits limits = {});
  bool install_manifest(const LanguageSetManifest& manifest, std::string& error);
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
  std::map<std::string, std::string> session_pins_;
};

}  // namespace surge
