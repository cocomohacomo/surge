import copy
import json
import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.run_structured_model_matrix import (
    build_completion_command,
    configured_json_retries,
    configured_threads,
    classify_generation_failure,
    detect_execution_error,
    extract_json,
    parse_cli_streams,
    run_command,
    validate_matrix_corpus,
    validate_model_lock,
    validate_runtime_config,
)


REPO_ROOT = Path(__file__).resolve().parents[1]


def valid_model_lock():
    return {
        "lock_version": "surge-model-matrix-lock-v1.0",
        "llama_cpp": {
            "repository": "https://github.com/ggml-org/llama.cpp",
            "commit": "fd4b137e38c9fdb24b3f4fe3bc9e5fcc3c8f0002",
        },
        "models": [{
            "name": "tiny-model",
            "repo": "org/model",
            "revision": "d86af45",
            "tokenizer_repo": "org/model",
            "tokenizer_revision": "d86af45",
            "file": "model-q4_k_m.gguf",
            "enabled": True,
        }],
    }


class StructuredRunnerOutputTests(unittest.TestCase):
    def test_import_does_not_require_llama_cpp_commit(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "LLAMA_CPP_COMMIT"):
                validate_runtime_config()

    def test_current_model_lock_is_valid(self):
        lock = json.loads((REPO_ROOT / "config/model-matrix-lock-v1.0.json").read_text(encoding="utf-8"))
        self.assertEqual(len(validate_model_lock(lock)), 2)

    def test_duplicate_model_name_is_rejected(self):
        lock = valid_model_lock()
        lock["models"].append(copy.deepcopy(lock["models"][0]))
        with self.assertRaisesRegex(RuntimeError, "duplicate model name"):
            validate_model_lock(lock)

    def test_model_path_traversal_is_rejected(self):
        lock = valid_model_lock()
        lock["models"][0]["file"] = "../outside/model.gguf"
        with self.assertRaisesRegex(RuntimeError, "unsafe GGUF"):
            validate_model_lock(lock)

    def test_mutable_revision_is_rejected(self):
        lock = valid_model_lock()
        lock["models"][0]["revision"] = "main"
        with self.assertRaisesRegex(RuntimeError, "pinned commit"):
            validate_model_lock(lock)

    def test_enabled_must_be_boolean(self):
        lock = valid_model_lock()
        lock["models"][0]["enabled"] = "false"
        with self.assertRaisesRegex(RuntimeError, "must be a boolean"):
            validate_model_lock(lock)

    def test_seed_gold_corpus_has_valid_unique_cases_and_evidence(self):
        corpus = json.loads(
            (REPO_ROOT / "tests/fixtures/semantic/gold_seed_v0.1.json").read_text(encoding="utf-8")
        )
        self.assertEqual(len(validate_matrix_corpus(corpus)), 8)

    def test_duplicate_gold_case_id_is_rejected(self):
        corpus = json.loads(
            (REPO_ROOT / "tests/fixtures/semantic/gold_seed_v0.1.json").read_text(encoding="utf-8")
        )
        corpus["cases"][1]["case_id"] = corpus["cases"][0]["case_id"]
        with self.assertRaisesRegex(RuntimeError, "duplicate case_id"):
            validate_matrix_corpus(corpus)

    def test_sampler_configuration_matches_manifest(self):
        command = build_completion_command("model.gguf", "/tmp/prompt.txt", cli_path="/bin/llama-completion", threads=2)
        self.assertEqual(command[command.index("--temp") + 1], "0.2")
        self.assertEqual(command[command.index("--top-k") + 1], "0")
        self.assertEqual(command[command.index("-n") + 1], "768")

    def test_noninteractive_chatml_uses_completion_binary(self):
        command = build_completion_command(
            "model.gguf", "/tmp/surge-prompt.txt", cli_path="/bin/llama-completion", threads=2
        )
        self.assertEqual(command[2], "/bin/llama-completion")
        self.assertIn("--no-conversation", command)
        self.assertIn("--json-schema-file", command)
        self.assertIn("-f", command)
        self.assertEqual(command[command.index("-f") + 1], "/tmp/surge-prompt.txt")
        self.assertNotIn("-p", command)
        self.assertNotIn("--single-turn", command)
        self.assertNotIn("--no-show-timings", command)

    def test_completion_binary_is_the_default(self):
        command = build_completion_command("model.gguf", "/tmp/prompt.txt", threads=2)
        self.assertTrue(command[2].endswith("llama-completion"))

    def test_evidence_span_validator_rejects_wrong_quote_offsets(self):
        from tools.run_structured_model_matrix import validate_evidence_spans
        case = {"text": "締切は水曜日。", "context": "前の期限は火曜日。"}
        valid = {"attributes": [{"key": "deadline", "value": "水曜日",
                 "evidence_spans": [{"source": "text", "start": 3, "end": 6, "quote": "水曜日"}]}],
                 "relations": [], "phenomena": [], "abstentions": []}
        self.assertTrue(validate_evidence_spans(valid, case)["valid"])
        invalid = {"attributes": [{"key": "deadline", "value": "水曜日",
                 "evidence_spans": [{"source": "text", "start": 3, "end": 6, "quote": "火曜日"}]}],
                 "relations": [], "phenomena": [], "abstentions": []}
        self.assertFalse(validate_evidence_spans(invalid, case)["valid"])
        malformed = {"attributes": [{"key": "x", "value": "x", "evidence_spans": [
            {"source": [], "start": 0, "end": 1, "quote": "x"}]}],
            "relations": [], "phenomena": [], "abstentions": []}
        self.assertFalse(validate_evidence_spans(malformed, {"text": "x", "context": ""})["valid"])

    def test_evidence_validator_rejects_bool_offsets_and_missing_spans(self):
        from tools.run_structured_model_matrix import validate_evidence_spans
        case = {"text": "abc", "context": ""}
        prediction = {"attributes": [{"key": "x", "value": "a", "evidence_spans": [
            {"source": "text", "start": True, "end": 1, "quote": "a"}]}],
            "relations": [], "phenomena": [], "abstentions": []}
        self.assertFalse(validate_evidence_spans(prediction, case)["valid"])
        prediction["attributes"][0]["evidence_spans"] = []
        self.assertFalse(validate_evidence_spans(prediction, case)["valid"])

    def test_evidence_offsets_are_source_specific_for_context(self):
        from tools.run_structured_model_matrix import validate_evidence_spans
        case = {"text": "締切は水曜日。", "context": "旧期限は火曜日。"}
        prediction = {"attributes": [{"key": "old_deadline", "value": "火曜日",
            "evidence_spans": [{"source": "context", "start": 4, "end": 7, "quote": "火曜日"}]}],
            "relations": [], "phenomena": [], "abstentions": []}
        self.assertTrue(validate_evidence_spans(prediction, case)["valid"])
        prediction["attributes"][0]["evidence_spans"][0]["source"] = "text"
        self.assertFalse(validate_evidence_spans(prediction, case)["valid"])

    def test_prompt_content_is_not_exposed_in_process_arguments(self):
        command = build_completion_command("model.gguf", "/tmp/prompt.txt", threads=2)
        self.assertNotIn("-p", command)
        self.assertNotIn("<|im_start|>", command)
        self.assertIn("/tmp/prompt.txt", command)

    def test_invalid_thread_count_is_rejected(self):
        with patch.dict(os.environ, {"SURGE_MODEL_THREADS": "0"}):
            with self.assertRaisesRegex(RuntimeError, "positive integer"):
                configured_threads()
        with patch.dict(os.environ, {"SURGE_MODEL_THREADS": "many"}):
            with self.assertRaisesRegex(RuntimeError, "positive integer"):
                configured_threads()

    def test_timeout_kills_and_reaps_process(self):
        completed, timed_out = run_command(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            timeout_seconds=0.1,
        )
        self.assertTrue(timed_out)
        self.assertIsNotNone(completed.returncode)

    def test_detects_sampler_failure_even_when_process_exit_is_zero(self):
        self.assertEqual(
            detect_execution_error("E srv send_error: Failed to initialize samplers: std::exception"),
            "sampler_initialization_failed",
        )

    def test_detects_unsupported_cli_argument(self):
        self.assertEqual(
            detect_execution_error("error: invalid argument: --no-conversation", 1),
            "llama_completion_invalid_argument",
        )

    def test_detects_schema_initialization_failure(self):
        self.assertEqual(
            detect_execution_error("failed to parse grammar", 1),
            "constrained_decoding_initialization_failed",
        )

    def test_nonzero_process_exit_is_not_accepted_as_json(self):
        result = parse_cli_streams('{"attributes":[]}', "", return_code=1)
        self.assertFalse(result["generation_stream_valid"])
        self.assertIsNone(result["parsed_output"])

    def test_nonfatal_diagnostics_do_not_fail_execution(self):
        self.assertIsNone(detect_execution_error("model loaded; generation complete", 0))

    def test_accepts_one_complete_json_object(self):
        self.assertEqual(
            extract_json('{"attributes":[],"relations":[]}'),
            {"attributes": [], "relations": []},
        )

    def test_accepts_pinned_completion_end_marker(self):
        self.assertEqual(
            extract_json('{"attributes":[],"relations":[]} [end of text]\n\n'),
            {"attributes": [], "relations": []},
        )

    def test_rejects_unrecognized_text_before_completion_marker(self):
        self.assertIsNone(
            extract_json('llama.cpp banner\n{"attributes":[],"relations":[]} [end of text]')
        )

    def test_rejects_banner_before_json(self):
        self.assertIsNone(extract_json('llama.cpp banner\n{"attributes":[],"relations":[]}'))

    def test_rejects_prompt_echo_with_stray_braces(self):
        self.assertIsNone(extract_json('Prompt {not-json}\n{"attributes":[],"relations":[]}'))

    def test_rejects_markdown_fence(self):
        fence = chr(96) * 3
        self.assertIsNone(extract_json(fence + 'json\n{"attributes":[],"relations":[]}\n' + fence))

    def test_rejects_multiple_objects(self):
        self.assertIsNone(extract_json('{"a":1}{"b":2}'))

    def test_rejects_non_object_json(self):
        self.assertIsNone(extract_json('[]'))

    def test_cli_diagnostics_are_never_merged_into_generation(self):
        result = parse_cli_streams('{"attributes":[],"relations":[]}', "sampler initialized")
        self.assertTrue(result["generation_stream_valid"])
        self.assertEqual(result["parsed_output"]["attributes"], [])
        self.assertIn("sampler initialized", result["diagnostic_stderr"])

    def test_banner_or_prompt_echo_on_stdout_is_rejected(self):
        result = parse_cli_streams('llama.cpp banner\n{"attributes":[],"relations":[]}', "")
        self.assertFalse(result["generation_stream_valid"])
        self.assertIsNone(result["parsed_output"])


    def test_retry_count_is_bounded_by_configuration(self):
        with patch.dict(os.environ, {"SURGE_JSON_MAX_RETRIES": "2"}):
            self.assertEqual(configured_json_retries(), 2)
        for value in ("-1", "3", "many"):
            with patch.dict(os.environ, {"SURGE_JSON_MAX_RETRIES": value}):
                with self.assertRaisesRegex(RuntimeError, "from 0 to 2"):
                    configured_json_retries()

    def test_generation_failure_reasons_are_classified(self):
        self.assertEqual(classify_generation_failure("", None, 0, False, None), "empty_output")
        self.assertEqual(classify_generation_failure("{broken", None, 0, False, None), "invalid_json")
        self.assertEqual(classify_generation_failure("not-json", None, 0, False, None), "non_json_output")
        self.assertEqual(classify_generation_failure("[]", None, 0, False, None), "json_not_object")
        self.assertEqual(classify_generation_failure("{}", {}, 0, False, None, [{"path": "/schema"}]),
                         "schema_validation_failed")
        self.assertEqual(classify_generation_failure("{}", None, 2, False, "llama_completion_nonzero_exit"),
                         "execution_error:llama_completion_nonzero_exit")

    def test_invalid_json_retries_once_and_preserves_both_attempts(self):
        import subprocess
        from tools.run_structured_model_matrix import run
        valid_output = json.dumps({
            "schema_version": "surge-projector-output-v1.0",
            "attributes": [], "relations": [],
        })
        responses = [
            (subprocess.CompletedProcess(["fake"], 0, "{broken", ""), False),
            (subprocess.CompletedProcess(["fake"], 0, valid_output, ""), False),
        ]
        seen_prompts = []

        def fake_run(command, timeout_seconds):
            prompt_path = Path(command[command.index("-f") + 1])
            seen_prompts.append(prompt_path.read_text(encoding="utf-8"))
            self.assertIn("--json-schema-file", command)
            self.assertEqual(command[command.index("--json-schema-file") + 1].endswith(
                "surge-projector-output-v1.0.schema.json"), True)
            return responses.pop(0)

        case = {"case_id": "retry-case", "text": "予定は金曜日。", "context": "", "gold": {
            "schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": [],
        }}
        with patch("tools.run_structured_model_matrix.run_command", side_effect=fake_run), \
             patch("tools.run_structured_model_matrix.validate_projector_output", return_value=[]), \
             patch("tools.run_structured_model_matrix.evaluate_case", return_value={"stub": True}):
            result = run({"name": "test-model"}, "model.gguf", case, max_json_retries=1)

        self.assertEqual(len(seen_prompts), 2)
        self.assertNotIn("前回出力は", seen_prompts[0])
        self.assertIn("前回出力はJSONオブジェクトとして読み取れませんでした", seen_prompts[1])
        self.assertEqual(result["retry_count"], 1)
        self.assertTrue(result["retry_recovered"])
        self.assertFalse(result["retry_exhausted"])
        self.assertFalse(result["attempts"][0]["json_valid"])
        self.assertEqual(result["attempts"][0]["failure_reason"], "invalid_json")
        self.assertTrue(result["attempts"][1]["json_valid"])
        self.assertTrue(result["json_valid"])
        self.assertEqual(result["final_attempt_number"], 2)

    def test_retry_limit_is_enforced_and_exhaustion_is_visible(self):
        import subprocess
        from tools.run_structured_model_matrix import run
        responses = [
            (subprocess.CompletedProcess(["fake"], 0, "{bad", ""), False),
            (subprocess.CompletedProcess(["fake"], 0, "{still-bad", ""), False),
        ]
        calls = []

        def fake_run(command, timeout_seconds):
            calls.append(command)
            return responses.pop(0)

        case = {"case_id": "retry-limit", "text": "x", "context": "", "gold": {
            "schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": [],
        }}
        with patch("tools.run_structured_model_matrix.run_command", side_effect=fake_run), \
             patch("tools.run_structured_model_matrix.validate_projector_output", return_value=[]):
            result = run({"name": "test-model"}, "model.gguf", case, max_json_retries=1)

        self.assertEqual(len(calls), 2)
        self.assertEqual(result["retry_count"], 1)
        self.assertTrue(result["retry_exhausted"])
        self.assertFalse(result["retry_recovered"])
        self.assertEqual(result["generation_failure_reason"], "invalid_json")

    def test_runtime_failure_is_not_retried(self):
        import subprocess
        from tools.run_structured_model_matrix import run
        calls = []
        def fake_run(command, timeout_seconds):
            calls.append(command)
            return subprocess.CompletedProcess(["fake"], 2, "", "model failure"), False

        case = {"case_id": "runtime-error", "text": "x", "context": "", "gold": {
            "schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": [],
        }}
        with patch("tools.run_structured_model_matrix.run_command", side_effect=fake_run):
            result = run({"name": "test-model"}, "model.gguf", case, max_json_retries=2)

        self.assertEqual(len(calls), 1)
        self.assertEqual(result["retry_count"], 0)
        self.assertEqual(result["execution_error"], "llama_completion_nonzero_exit")
        self.assertEqual(result["generation_failure_reason"],
                         "execution_error:llama_completion_nonzero_exit")


if __name__ == "__main__":
    unittest.main()
