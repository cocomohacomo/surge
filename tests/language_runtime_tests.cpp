#include "surge/language_runtime.hpp"
#include <cassert>
#include <string>

int main() {
  using namespace surge;
  LanguageRuntimeLimits limits;
  limits.max_installed_sets = 2;
  limits.max_sessions = 1;
  limits.max_package_bytes = 1000;
  limits.max_total_unpacked_bytes = 2000;
  limits.max_total_resident_bytes = 1000;
  limits.max_scratch_bytes = 256;
  LanguageRuntimeRegistry runtime(limits);
  LanguageSetManifest ja{"ja-JP", "1.0.0", "sha256:ja", "ja-JP", "Jpan",
      300, 600, 200, 64, 4096, 64, {"kana-kanji", "normalization"}};
  LanguageSetManifest en{"en-US", "1.0.0", "sha256:en", "en-US", "Latn",
      300, 600, 200, 64, 4096, 64, {"lexicon", "normalization"}};
  std::string error;
  assert(runtime.install_manifest(ja, error));
  assert(runtime.install_manifest(en, error));
  assert(runtime.installed_count() == 2);
  assert(!runtime.activate_at_boundary("session-1", "ja-JP", "1.0.0", false, error));
  assert(error == "LANGUAGE_SWITCH_REQUIRES_SESSION_BOUNDARY");
  assert(runtime.activate_at_boundary("session-1", "ja-JP", "1.0.0", true, error));
  LanguageSetManifest active;
  assert(runtime.active_manifest("session-1", active, error));
  assert(active.language_set_id == "ja-JP");
  assert(!runtime.uninstall("ja-JP", "1.0.0", error));
  assert(error == "LANGUAGE_SET_PINNED_BY_SESSION");
  assert(!runtime.activate_at_boundary("session-2", "en-US", "1.0.0", true, error));
  assert(error == "LANGUAGE_SESSION_BUDGET_EXCEEDED");
  assert(runtime.activate_at_boundary("session-1", "en-US", "1.0.0", true, error));
  assert(runtime.active_manifest("session-1", active, error));
  assert(active.language_set_id == "en-US");
  assert(runtime.release_session("session-1"));
  assert(runtime.uninstall("ja-JP", "1.0.0", error));
  LanguageSetManifest oversized = en;
  oversized.language_set_id = "too-large";
  oversized.resident_bytes = 900;
  assert(!runtime.install_manifest(oversized, error));
  assert(error == "LANGUAGE_SET_RESOURCE_BUDGET_EXCEEDED");
  return 0;
}
