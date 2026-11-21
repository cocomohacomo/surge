#include "surge/language_runtime.hpp"
#include "surge/sha256.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
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
  LanguageSetManifest ja{"ja-JP", "1.0.0", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "ja-JP", "Jpan",
      300, 600, 200, 64, 4096, 64, {"kana-kanji", "normalization"}};
  LanguageSetManifest en{"en-US", "1.0.0", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "en-US", "Latn",
      300, 600, 200, 64, 4096, 64, {"lexicon", "normalization"}};
  std::string error;
  LanguageSetManifest malformed_digest = ja;
  malformed_digest.language_set_id = "bad-digest";
  malformed_digest.digest = "sha256:not-a-digest";
  assert(!runtime.install_manifest(malformed_digest, error));
  assert(error == "LANGUAGE_SET_MANIFEST_INVALID");
  assert(runtime.installed_count() == 0);
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
  LanguageSetManifest oversized_input = ja;
  oversized_input.language_set_id = "oversized-input";
  oversized_input.max_input_bytes = limits.max_input_bytes + 1;
  assert(!runtime.install_manifest(oversized_input, error));
  assert(error == "LANGUAGE_SET_MANIFEST_INVALID");
  {
    ResourceBudget budget({512, 64, 16, 8, 64, 128});
    LanguageRuntimeRegistry bounded(budget, limits);
    LanguageSetManifest pack = ja;
    pack.language_set_id = "ja-budget";
    pack.resident_bytes = 400;
    pack.scratch_bytes = 64;
    assert(bounded.install_manifest(pack, error));
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::ResidentBytes)] == 400);
    assert(bounded.activate_at_boundary("bounded-session", "ja-budget", "1.0.0", true, error));
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::TransientBytes)] == 64);
    LanguageSetManifest second = pack;
    second.language_set_id = "en-budget";
    second.resident_bytes = 100;
    assert(bounded.install_manifest(second, error));
    assert(!bounded.activate_at_boundary("bounded-session", "en-budget", "1.0.0", true, error));
    LanguageSetManifest still_active;
    assert(bounded.active_manifest("bounded-session", still_active, error));
    assert(still_active.language_set_id == "ja-budget");
    assert(bounded.release_session("bounded-session"));
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::TransientBytes)] == 0);
    assert(bounded.uninstall("ja-budget", "1.0.0", error));
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::ResidentBytes)] == 100);
    assert(bounded.uninstall("en-budget", "1.0.0", error));
    assert(budget.snapshot().used[static_cast<std::size_t>(ResourceKind::ResidentBytes)] == 0);
  }

  {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "surge-language-pack-verification";
    fs::remove_all(root);
    fs::create_directories(root / "data");
    const std::string payload = "SURGE-DATA-ONLY-RESOURCE";
    {
      std::ofstream out(root / "data" / "lexicon.bin", std::ios::binary);
      out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    LanguageSetManifest package = ja;
    package.package_bytes = 512;
    package.unpacked_bytes = payload.size();
    package.resources = {{"data/lexicon.bin", "lexicon", sha256_hex(payload),
                          payload.size(), true}};
    assert(verify_language_package_directory(root.string(), package, limits, error));
    {
      std::ofstream out(root / "data" / "lexicon.bin", std::ios::binary | std::ios::trunc);
      out << "tampered";
    }
    assert(!verify_language_package_directory(root.string(), package, limits, error));
    assert(error == "LANGUAGE_PACKAGE_RESOURCE_SIZE_MISMATCH" ||
           error == "LANGUAGE_PACKAGE_RESOURCE_DIGEST_MISMATCH");
    package.resources[0].path = "../outside.bin";
    assert(!verify_language_package_directory(root.string(), package, limits, error));
    assert(error == "LANGUAGE_PACKAGE_RESOURCE_DESCRIPTOR_INVALID");
    package.resources[0].path = "data/lexicon.bin";
    package.resources[0].kind = "native_module";
    assert(!verify_language_package_directory(root.string(), package, limits, error));
    assert(error == "LANGUAGE_PACKAGE_RESOURCE_DESCRIPTOR_INVALID");
    fs::remove_all(root);
  }
  return 0;
}
