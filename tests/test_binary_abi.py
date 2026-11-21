import unittest

from tools.check_binary_abi import parse_symbols


class BinaryAbiSymbolParsingTests(unittest.TestCase):
    def test_elf_symbols_keep_only_public_surge_exports(self):
        output = """0000000000001000 T surge_abi_version
0000000000001010 T surge_packet_core_open
0000000000001020 t internal_helper
                 U std::runtime_error
"""
        self.assertEqual(
            parse_symbols(output, "linux"),
            {"surge_abi_version", "surge_packet_core_open"},
        )

    def test_macho_leading_underscore_is_normalized(self):
        output = """0000000000001000 T _surge_abi_version
0000000000001010 T _surge_packet_core_open
"""
        self.assertEqual(
            parse_symbols(output, "darwin"),
            {"surge_abi_version", "surge_packet_core_open"},
        )

    def test_windows_dumpbin_export_table(self):
        output = """ordinal hint RVA      name
      1    0 00001000 surge_abi_version
      2    1 00001010 surge_packet_core_open
"""
        self.assertEqual(
            parse_symbols(output, "win32"),
            {"surge_abi_version", "surge_packet_core_open"},
        )


if __name__ == "__main__":
    unittest.main()
