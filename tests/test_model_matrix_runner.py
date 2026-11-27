import os
import unittest
from unittest.mock import patch

from tools.run_structured_model_matrix import build_cli_command, detect_execution_error, extract_json, parse_cli_streams, validate_runtime_config


class StructuredRunnerOutputTests(unittest.TestCase):
    def test_import_does_not_require_llama_cpp_commit(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "LLAMA_CPP_COMMIT"):
                validate_runtime_config()

    def test_sampler_configuration_matches_manifest_and_disables_top_k(self):
        command = build_cli_command("model.gguf", "prompt")
        self.assertEqual(command[command.index("--temp") + 1], "0.8")
        self.assertEqual(command[command.index("--top-k") + 1], "0")

    def test_preformatted_chatml_prompt_disables_automatic_conversation_wrapping(self):
        command = build_cli_command("model.gguf", "<|im_start|>assistant")
        self.assertIn("--no-conversation", command)
        self.assertNotIn("--single-turn", command)

    def test_detects_sampler_failure_even_when_cli_exits_zero(self):
        self.assertEqual(
            detect_execution_error("E srv send_error: Failed to initialize samplers: std::exception"),
            "sampler_initialization_failed",
        )

    def test_nonfatal_diagnostics_do_not_fail_execution(self):
        self.assertIsNone(detect_execution_error("model loaded; generation complete"))

    def test_accepts_one_complete_json_object(self):
        self.assertEqual(extract_json('{"attributes":[],"relations":[]}'),
                         {"attributes": [], "relations": []})

    def test_rejects_banner_before_json(self):
        self.assertIsNone(extract_json('llama.cpp banner\n{"attributes":[],"relations":[]}'))

    def test_rejects_prompt_echo_with_stray_braces(self):
        self.assertIsNone(extract_json('Prompt {not-json}\n{"attributes":[],"relations":[]}'))

    def test_rejects_markdown_fence(self):
        self.assertIsNone(extract_json('```json\n{"attributes":[],"relations":[]}\n```'))

    def test_rejects_multiple_objects(self):
        self.assertIsNone(extract_json('{"a":1}{"b":2}'))

    def test_rejects_non_object_json(self):
        self.assertIsNone(extract_json('[]'))


    def test_cli_diagnostics_are_never_merged_into_generation(self):
        result = parse_cli_streams('{"attributes":[],"relations":[]}', 'llama.cpp banner\\nsampler initialized')
        self.assertTrue(result["generation_stream_valid"])
        self.assertEqual(result["parsed_output"]["attributes"], [])
        self.assertIn("sampler initialized", result["diagnostic_stderr"])

    def test_banner_or_prompt_echo_on_stdout_is_rejected(self):
        result = parse_cli_streams('llama.cpp banner\\n{"attributes":[],"relations":[]}', '')
        self.assertFalse(result["generation_stream_valid"])
        self.assertIsNone(result["parsed_output"])


if __name__ == "__main__":
    unittest.main()
