import os
import pathlib
import tempfile
import unittest
from unittest.mock import patch

from surge_adapter._paths import native_core_candidates, resolve_native_core_library


class NativeCoreDiscoveryTests(unittest.TestCase):
    def test_explicit_library_path_has_highest_precedence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            explicit = root / "explicit" / "libsurge_core.so"
            configured = root / "configured" / "libsurge_core.so"
            explicit.parent.mkdir()
            configured.parent.mkdir()
            explicit.touch()
            configured.touch()
            with patch.dict(os.environ, {"SURGE_CORE_LIBRARY": str(configured)}):
                self.assertEqual(resolve_native_core_library(explicit), explicit.resolve())

    def test_cmake_prefix_install_is_discovered(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = pathlib.Path(tmp)
            installed = prefix / "lib" / "libsurge_core.so"
            installed.parent.mkdir()
            installed.touch()
            with patch.dict(os.environ, {"SURGE_CORE_PREFIX": str(prefix)}, clear=False):
                with patch("surge_adapter._paths.sys.platform", "linux"):
                    self.assertEqual(resolve_native_core_library(), installed.resolve())

    def test_missing_library_reports_supported_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch.dict(os.environ, {
                "SURGE_CORE_LIBRARY": str(pathlib.Path(tmp) / "missing.so"),
                "SURGE_CORE_PREFIX": str(pathlib.Path(tmp) / "empty-prefix"),
            }):
                with self.assertRaisesRegex(FileNotFoundError, "SURGE_CORE_LIBRARY_NOT_FOUND"):
                    resolve_native_core_library()

    def test_candidate_order_is_deterministic_and_deduplicated(self):
        with tempfile.TemporaryDirectory() as tmp:
            explicit = pathlib.Path(tmp) / "libsurge_core.so"
            explicit.touch()
            with patch.dict(os.environ, {"SURGE_CORE_LIBRARY": str(explicit)}):
                candidates = native_core_candidates(explicit)
            self.assertEqual(candidates[0], explicit)
            self.assertEqual(len(candidates), len(set(map(str, candidates))))


if __name__ == "__main__":
    unittest.main()
