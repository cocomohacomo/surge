#!/usr/bin/env python3
"""Pinned CPU model matrix with constrained decoding and strict post-validation.

The synthetic seed corpus is engineering-only. Recognition qualification remains
NOT_RUN until independently adjudicated held-out data is supplied.
"""
import hashlib, json, os, platform, re, shutil, signal, subprocess, sys, tempfile, time
from pathlib import Path, PurePosixPath
from statistics import median

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from evaluate_gold_corpus import evaluate_case
from validate_projector_output import validate as validate_projector_output

LOCK_PATH = Path(os.environ.get("SURGE_MODEL_MATRIX_LOCK", "config/model-matrix-lock-v1.0.json"))
if not LOCK_PATH.is_absolute():
    LOCK_PATH = REPO_ROOT / LOCK_PATH
ROOT = Path(os.environ.get("SURGE_MODEL_MATRIX_DIR", "model-matrix"))
OUT = Path(os.environ.get("SURGE_MODEL_MATRIX_REPORT", ROOT / "model-matrix.json"))
COMPLETION_CLI = os.environ.get("LLAMA_COMPLETION_CLI", "llama-completion")
TIMEOUT_SECONDS = 240
MAX_GENERATION_TOKENS = 768
GENERATION_TEMPERATURE = 0.2
GENERATION_TOP_K = 0
DEFAULT_JSON_MAX_RETRIES = 1
HARD_JSON_MAX_RETRIES = 2
RETRYABLE_GENERATION_FAILURES = {
    "empty_output", "invalid_json", "non_json_output",
    "json_not_object", "schema_validation_failed", "evidence_validation_failed",
}
SCHEMA_PATH = REPO_ROOT / "schemas" / "surge-projector-output-v1.0.schema.json"

# Runtime assets are loaded only by main(), keeping imports deterministic and testable.
LOCK = None
ALL_MODELS = []
MODELS = []
EXCLUDED_MODELS = []
LOCKED_LLAMA_COMMIT = None
SCHEMA = None


def validate_model_lock(lock):
    """Validate model identity and file paths before any remote model download."""
    if not isinstance(lock, dict) or lock.get("lock_version") != "surge-model-matrix-lock-v1.0":
        raise RuntimeError("unsupported or malformed model lock")
    llama = lock.get("llama_cpp")
    if not isinstance(llama, dict):
        raise RuntimeError("model lock must contain llama_cpp metadata")
    if (llama.get("repository") != "https://github.com/ggml-org/llama.cpp"
            or not isinstance(llama.get("commit"), str)
            or not re.fullmatch(r"[0-9a-f]{40}", llama["commit"])):
        raise RuntimeError("llama.cpp repository and full immutable commit are required")
    models = lock.get("models")
    if not isinstance(models, list) or not models:
        raise RuntimeError("model lock must contain a non-empty models array")
    names = set()
    enabled_count = 0
    required_strings = ("name", "repo", "revision", "tokenizer_repo",
                        "tokenizer_revision", "file")
    for index, model in enumerate(models):
        if not isinstance(model, dict):
            raise RuntimeError(f"model lock entry {index} must be an object")
        for field in required_strings:
            value = model.get(field)
            if not isinstance(value, str) or not value.strip():
                raise RuntimeError(f"model lock entry {index} has invalid {field}")
        name = model["name"]
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
            raise RuntimeError(f"unsafe model name: {name!r}")
        if name in names:
            raise RuntimeError(f"duplicate model name: {name}")
        names.add(name)
        for field in ("repo", "tokenizer_repo"):
            if not re.fullmatch(r"[^/\s]+/[^/\s]+", model[field]):
                raise RuntimeError(f"invalid Hugging Face repository in {field}: {model[field]!r}")
        for field in ("revision", "tokenizer_revision"):
            if not re.fullmatch(r"[0-9a-fA-F]{7,40}", model[field]):
                raise RuntimeError(f"{field} must be a pinned commit prefix or full SHA")
        filename = model["file"]
        relative_path = PurePosixPath(filename)
        if (relative_path.is_absolute() or ".." in relative_path.parts
                or "\\" in filename or relative_path.suffix.lower() != ".gguf"):
            raise RuntimeError(f"unsafe GGUF model file path: {filename!r}")
        enabled = model.get("enabled", True)
        if not isinstance(enabled, bool):
            raise RuntimeError(f"model {name} enabled must be a boolean")
        if enabled:
            enabled_count += 1
        elif not isinstance(model.get("exclusion_reason"), str) or not model["exclusion_reason"].strip():
            raise RuntimeError(f"disabled model {name} requires an exclusion_reason")
    if enabled_count == 0:
        raise RuntimeError("model lock must enable at least one model")
    return models


def configured_threads():
    raw = os.environ.get("SURGE_MODEL_THREADS", "2")
    try:
        value = int(raw)
    except (TypeError, ValueError) as error:
        raise RuntimeError("SURGE_MODEL_THREADS must be a positive integer") from error
    if value < 1:
        raise RuntimeError("SURGE_MODEL_THREADS must be a positive integer")
    return value


def configured_json_retries():
    """Return a bounded retry count; the first generation is never counted as a retry."""
    raw = os.environ.get("SURGE_JSON_MAX_RETRIES", str(DEFAULT_JSON_MAX_RETRIES))
    try:
        value = int(raw)
    except (TypeError, ValueError) as error:
        raise RuntimeError("SURGE_JSON_MAX_RETRIES must be an integer from 0 to 2") from error
    if value < 0 or value > HARD_JSON_MAX_RETRIES:
        raise RuntimeError("SURGE_JSON_MAX_RETRIES must be an integer from 0 to 2")
    return value


def load_runtime_assets():
    global LOCK, ALL_MODELS, MODELS, EXCLUDED_MODELS, LOCKED_LLAMA_COMMIT, SCHEMA
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    models = validate_model_lock(lock)
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    if not isinstance(schema, dict):
        raise RuntimeError("canonical projector schema must be a JSON object")
    LOCK = lock
    ALL_MODELS = models
    MODELS = [model for model in models if model.get("enabled", True)]
    EXCLUDED_MODELS = [
        {"name": model["name"], "reason": model["exclusion_reason"]}
        for model in models if not model.get("enabled", True)
    ]
    LOCKED_LLAMA_COMMIT = lock["llama_cpp"]["commit"]
    SCHEMA = schema
    return LOCK
def validate_runtime_config():
    """Load and validate pinned runtime settings; importing helpers performs no I/O."""
    load_runtime_assets()
    llama_cpp_commit = os.environ.get("LLAMA_CPP_COMMIT", "")
    if (not isinstance(LOCKED_LLAMA_COMMIT, str)
            or not re.fullmatch(r"[0-9a-f]{40}", LOCKED_LLAMA_COMMIT)
            or llama_cpp_commit != LOCKED_LLAMA_COMMIT):
        raise RuntimeError(
            "LLAMA_CPP_COMMIT must match the full immutable commit in the model lock"
        )
    return llama_cpp_commit

SYSTEM = """あなたはSURGEの外部認識Projectorです。入力に明示された情報だけを抽出し、真偽や常識を補完しません。
出力は指定されたCanonical JSON Schemaに適合するJSONオブジェクト1個だけです。Markdown、コードフェンス、説明文は禁止です。
attributesは入力から直接支持される属性だけを格納します。keyは属性名、valueは原文に沿った値、polarityはpositive/negative/uncertain/unknown、categoryは意味カテゴリです。
relationsは原文が明示する二項関係だけを格納します。typeは関係名、sourceとtargetは関係の両端です。関係を推測で補いません。
evidence_spansは各属性・関係を直接支持する最短の連続原文範囲です。sourceはtextまたはcontext、startは0始まりUnicodeコードポイント位置、endは排他的、quoteは原文[source][start:end]と完全一致させます。1つの属性に複数根拠が必要なら複数spanを付けます。
根拠範囲は各出力対象の値・意味・現象を直接支持する原文内の最短連続範囲にしてください。offsetは入力ごとに原文から計算し、プロンプト内の他の文章のoffset・quoteを流用しません。出力前に source[start:end] と quote の完全一致を確認してください。contextとtextは別々の原文としてoffsetを計算します。情報を推測せず、曖昧なら属性・関係・現象を無理に出力せずabstentionsを使用してください。
空配列と判断保留を区別します。該当情報が入力に存在しない場合、その配列は空にします。情報は存在するが指示対象・値・極性・関係が曖昧、矛盾、または確定不能な場合は、推測で属性や関係を作らずabstentionsにreasonを記録します。単に情報がないことをabstentionにしません。
現象判定はattributes/relationから推測せず、phenomena配列に独立して出力します。各要素はlabelと、その現象を直接支持するEvidence Spanを持ちます。4現象はnegation、contradiction、temporal_supersession、ambiguityです。根拠がなければ出力しません。\nEvidenceが取れない属性・関係・現象は出力しません。空配列は許容されます。"""

PROMPT_TEMPLATE = (
    "<|im_start|>system\n{system}<|im_end|>\n"
    "<|im_start|>user\n"
    "Context原文: {context}\n"
    "Input原文: {text}\n"
    "上記原文のみを根拠にCanonical JSONを返してください。"
    "属性・関係を捏造せず、Evidence Spanのoffsetとquoteを照合してください。"
    "情報なしは空配列、情報はあるが判断不能はabstentionsです。"
    "{retry_hint}{mode_hint}<|im_end|>\n<|im_start|>assistant\n"
)

def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def extract_json(text):
    """Accept only a complete JSON object from the generation-only stdout stream."""
    if not isinstance(text, str):
        return None
    candidate = text.strip()
    if not candidate:
        return None
    try:
        value = json.loads(candidate)
    except json.JSONDecodeError:
        # The pinned llama-completion emits this exact non-JSON suffix after EOS.
        # Remove only that terminal marker; all other output remains strict JSON input.
        marker = " [end of text]"
        if not candidate.endswith(marker):
            return None
        candidate = candidate[:-len(marker)].rstrip()
        try:
            value = json.loads(candidate)
        except json.JSONDecodeError:
            return None
    return value if isinstance(value, dict) else None


def validate_matrix_corpus(corpus):
    """Reject malformed, duplicated or incorrectly offset Gold examples before model runs."""
    if not isinstance(corpus, dict) or corpus.get("schema_version") != "surge-gold-corpus-v1.0":
        raise RuntimeError("unsupported or malformed Gold corpus")
    cases = corpus.get("cases")
    if not isinstance(cases, list) or not cases:
        raise RuntimeError("Gold corpus must contain at least one case")
    seen = set()
    for index, case in enumerate(cases):
        if not isinstance(case, dict):
            raise RuntimeError(f"Gold case {index} must be an object")
        case_id = case.get("case_id")
        if not isinstance(case_id, str) or not case_id.strip() or case_id in seen:
            raise RuntimeError(f"Gold case {index} has a missing or duplicate case_id")
        seen.add(case_id)
        if not isinstance(case.get("text"), str) or not isinstance(case.get("context", ""), str):
            raise RuntimeError(f"Gold case {case_id} text/context must be strings")
        gold = case.get("gold")
        if not isinstance(gold, dict):
            raise RuntimeError(f"Gold case {case_id} has no canonical gold object")
        schema_errors = validate_projector_output(gold, SCHEMA_PATH)
        if schema_errors:
            raise RuntimeError(f"Gold case {case_id} violates the canonical schema: {schema_errors[0]}")
        sources = {"text": case.get("text", ""), "context": case.get("context", "")}
        for collection in ("attributes", "relations", "phenomena", "abstentions"):
            entries = gold.get(collection, [])
            if not isinstance(entries, list):
                raise RuntimeError(f"Gold case {case_id} {collection} must be an array")
            for entry in entries:
                if not isinstance(entry, dict):
                    raise RuntimeError(f"Gold case {case_id} contains a malformed {collection} entry")
                spans = entry.get("evidence_spans", [])
                if not isinstance(spans, list):
                    raise RuntimeError(f"Gold case {case_id} has malformed evidence_spans")
                for span in spans:
                    if not isinstance(span, dict):
                        raise RuntimeError(f"Gold case {case_id} has malformed evidence span")
                    source = span.get("source")
                    start, end, quote = span.get("start"), span.get("end"), span.get("quote")
                    if (source not in sources
                            or not isinstance(start, int) or isinstance(start, bool)
                            or not isinstance(end, int) or isinstance(end, bool)
                            or not isinstance(quote, str)
                            or start < 0 or end <= start or end > len(sources[source])
                            or sources[source][start:end] != quote):
                        raise RuntimeError(f"Gold case {case_id} contains an invalid evidence offset")
    return cases


def parse_cli_streams(stdout, stderr, return_code=0):
    """Accept generated JSON only from a successful, non-interactive completion process."""
    generated = extract_json(stdout) if return_code == 0 else None
    return {
        "parsed_output": generated,
        "generation_stream_valid": generated is not None,
        "diagnostic_stderr": stderr[-4000:] if isinstance(stderr, str) else "",
        "stdout_rejected": generated is None,
    }


def detect_execution_error(diagnostics, return_code=None):
    """Classify known runtime failures without confusing them with JSON-quality failures."""
    if not isinstance(diagnostics, str):
        diagnostics = ""
    if re.search(
        r"Failed to initialize samplers|failed to launch slot with task",
        diagnostics, re.IGNORECASE,
    ):
        return "sampler_initialization_failed"
    if re.search(r"invalid argument|unknown argument|unrecognized option",
                 diagnostics, re.IGNORECASE):
        return "llama_completion_invalid_argument"
    if re.search(r"failed to parse grammar|failed to parse schema|failed to open file",
                 diagnostics, re.IGNORECASE):
        return "constrained_decoding_initialization_failed"
    if re.search(r"failed to load model|error loading model|unable to load model",
                 diagnostics, re.IGNORECASE):
        return "model_load_failed"
    if return_code not in (None, 0):
        return "llama_completion_nonzero_exit"
    return None


def build_completion_command(model_path, prompt_path, cli_path=None, threads=None):
    """Build the non-interactive command using a UTF-8 prompt file, never argv prompt text."""
    binary = str(cli_path or COMPLETION_CLI)
    thread_count = configured_threads() if threads is None else threads
    prompt_file = str(prompt_path)
    if not prompt_file.strip():
        raise ValueError("prompt_path must not be empty")
    if not isinstance(thread_count, int) or isinstance(thread_count, bool) or thread_count < 1:
        raise ValueError("threads must be a positive integer")
    return [
        "/usr/bin/time", "-v", binary, "-m", str(model_path), "-f", prompt_file,
        "-n", str(MAX_GENERATION_TOKENS), "--temp", str(GENERATION_TEMPERATURE),
        "--top-k", str(GENERATION_TOP_K), "--threads", str(thread_count),
        "--no-conversation", "--no-display-prompt", "--simple-io",
        "--json-schema-file", str(SCHEMA_PATH),
    ]


def _signal_process_tree(process, sig):
    if os.name == "posix":
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass
        except OSError:
            if process.poll() is None:
                process.send_signal(sig)
    elif process.poll() is None:
        if sig == getattr(signal, "SIGKILL", signal.SIGTERM):
            process.kill()
        else:
            process.terminate()


def run_command(command, timeout_seconds=TIMEOUT_SECONDS):
    """Run a command in its own process group and reap it, including descendants, on timeout."""
    if (not isinstance(timeout_seconds, (int, float))
            or isinstance(timeout_seconds, bool) or timeout_seconds <= 0):
        raise ValueError("timeout_seconds must be positive")
    process = subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace",
        start_new_session=(os.name == "posix"),
    )
    try:
        stdout, stderr = process.communicate(timeout=timeout_seconds)
        timed_out = False
    except subprocess.TimeoutExpired:
        timed_out = True
        _signal_process_tree(process, signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            _signal_process_tree(process, getattr(signal, "SIGKILL", signal.SIGTERM))
            stdout, stderr = process.communicate()
    return subprocess.CompletedProcess(command, process.returncode, stdout, stderr), timed_out


def _read_diagnostic_log(log_path):
    if not log_path.exists():
        return ""
    try:
        return log_path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        return f"Unable to read llama-completion log: {error}"


def _write_json_atomic(path, payload):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".{os.getpid()}.tmp")
    try:
        with temporary.open("w", encoding="utf-8") as stream:
            json.dump(payload, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def _update_model_metrics(model_report):
    rows = model_report.get("results", [])
    elapsed = [row["elapsed_seconds"] for row in rows if "elapsed_seconds" in row]
    rss = [row["peak_rss_kib"] for row in rows if row.get("peak_rss_kib") is not None]
    attempts = [attempt for row in rows for attempt in row.get("attempts", [])]
    first_attempts = [row["attempts"][0] for row in rows if row.get("attempts")]
    retry_reasons = {}
    for attempt in attempts:
        reason = attempt.get("failure_reason")
        if reason:
            retry_reasons[reason] = retry_reasons.get(reason, 0) + 1
    model_report.update({
        "json_valid_cases": sum(bool(row.get("json_valid")) for row in rows),
        "schema_valid_cases": sum(bool(row.get("projector_schema_valid")) for row in rows),
        "coverage": sum(bool(row.get("engineering_semantic_metrics")) for row in rows) / len(rows) if rows else 0.0,
        "timeouts": sum(bool(row.get("timeout")) for row in rows),
        "latency_mean_seconds": sum(elapsed) / len(elapsed) if elapsed else None,
        "latency_p50_seconds": median(elapsed) if elapsed else None,
        "latency_max_seconds": max(elapsed) if elapsed else None,
        "peak_rss_kib_max": max(rss) if rss else None,
        "initial_attempt_json_valid_cases": sum(bool(item.get("json_valid")) for item in first_attempts),
        "initial_attempt_schema_valid_cases": sum(bool(item.get("projector_schema_valid")) for item in first_attempts),
        "initial_attempt_failures": sum(not (item.get("json_valid") and item.get("projector_schema_valid")) for item in first_attempts),
        "retry_cases": sum(int(row.get("retry_count", 0)) > 0 for row in rows),
        "retry_recovered_cases": sum(bool(row.get("retry_recovered")) for row in rows),
        "retry_exhausted_cases": sum(bool(row.get("retry_exhausted")) for row in rows),
        "retry_attempts_total": sum(int(row.get("retry_count", 0)) for row in rows),
        "retry_failure_reasons": retry_reasons,
    })
def validate_evidence_spans(prediction, case):
    """Validate emitted spans against the exact text/context source."""
    if not isinstance(prediction, dict) or not isinstance(case, dict):
        return {"valid": False, "span_count": 0, "errors": ["prediction_or_case_not_object"]}
    sources = {"text": case.get("text", ""), "context": case.get("context", "")}
    errors, span_count = [], 0
    for collection in ("attributes", "relations", "phenomena", "abstentions"):
        items = prediction.get(collection, [])
        if not isinstance(items, list):
            errors.append(collection + "_not_array")
            continue
        for item_index, item in enumerate(items):
            if not isinstance(item, dict):
                errors.append(f"{collection}[{item_index}]_not_object")
                continue
            spans = item.get("evidence_spans", [])
            if not isinstance(spans, list) or not spans:
                errors.append(f"{collection}[{item_index}]_missing_evidence")
                continue
            for span_index, span in enumerate(spans):
                span_count += 1
                path = f"{collection}[{item_index}].evidence_spans[{span_index}]"
                if not isinstance(span, dict):
                    errors.append(path + "_not_object")
                    continue
                source_name, start, end, quote = (span.get("source"), span.get("start"), span.get("end"), span.get("quote"))
                if (not isinstance(source_name, str) or source_name not in sources
                        or not isinstance(start, int) or isinstance(start, bool)
                        or not isinstance(end, int) or isinstance(end, bool)
                        or not isinstance(quote, str) or not quote or start < 0 or end <= start):
                    errors.append(path + "_invalid_fields")
                    continue
                text = sources[source_name]
                if not isinstance(text, str) or end > len(text) or text[start:end] != quote:
                    errors.append(path + "_quote_offset_mismatch")
    return {"valid": not errors, "span_count": span_count, "errors": errors}


def _retry_prompt_hint(failure_reason, schema_errors=None):
    """Build a short corrective hint without weakening the external JSON Schema constraint."""
    if failure_reason == "evidence_validation_failed":
        return (
            "\\n前回出力はJSON Schemaに適合しましたが、Evidence Spanのsource・start・end・quoteが原文と一致しませんでした。"
            "各属性・関係・現象・abstentionの意味を直接支持する最短の連続範囲を、"
            "textとcontextを別々に0始まりUnicodeコードポイントで数えて再計算してください。"
            "quoteは必ず該当source[start:end]と完全一致させ、根拠がない項目は出力しないでください。"
            "Canonical JSON Schemaの必須項目・型・enum・追加プロパティ禁止は変更しないでください。"
        )
    if failure_reason == "schema_validation_failed":
        paths = []
        for error in (schema_errors or [])[:3]:
            if isinstance(error, dict):
                paths.append(str(error.get("path", "/")))
        detail = " 対象パス: " + "、".join(paths) if paths else ""
        return (
            "\n前回出力はJSONでしたがCanonical JSON Schemaに不適合でした。"
            "必須項目・型・enum・追加プロパティ禁止を守り、スキーマ適合JSONオブジェクト1個だけを再生成してください。"
            + detail
        )
    return (
        "\n前回出力はJSONオブジェクトとして読み取れませんでした。"
        "説明・Markdown・コードフェンス・前置き・複数オブジェクトを出さず、"
        "Canonical JSON Schemaに適合するJSONオブジェクト1個だけを生成してください。"
    )


def classify_generation_failure(stdout, parsed, return_code, timed_out,
                                execution_error, schema_errors=None, evidence_valid=None):
    """Classify output failures separately from infrastructure/runtime failures."""
    if timed_out:
        return "generation_timeout"
    if execution_error:
        return "execution_error:" + execution_error
    if return_code != 0:
        return "process_nonzero_exit"
    candidate = stdout.strip() if isinstance(stdout, str) else ""
    if not candidate:
        return "empty_output"
    marker = " [end of text]"
    if candidate.endswith(marker):
        candidate = candidate[:-len(marker)].rstrip()
    try:
        decoded = json.loads(candidate)
    except (TypeError, json.JSONDecodeError):
        if candidate.startswith(("{", "[")):
            return "invalid_json"
        return "non_json_output"
    if not isinstance(decoded, dict):
        return "json_not_object"
    if parsed is None:
        return "non_json_output"
    if schema_errors:
        return "schema_validation_failed"
    if evidence_valid is False:
        return "evidence_validation_failed"
    return None


def run(model, model_path, case, max_json_retries=None):
    """Run one case with bounded JSON/Schema retries and preserve every attempt."""
    max_retries = configured_json_retries() if max_json_retries is None else max_json_retries
    if (not isinstance(max_retries, int) or isinstance(max_retries, bool)
            or max_retries < 0 or max_retries > HARD_JSON_MAX_RETRIES):
        raise ValueError("max_json_retries must be an integer from 0 to 2")

    mode_hint = "/no_think" if model.get("format") == "chatml_no_think" else ""
    prompt_values = {
        "system": SYSTEM, "context": case.get("context", ""),
        "text": case.get("text", ""), "mode_hint": mode_hint,
    }
    attempts = []
    total_started = time.perf_counter()

    with tempfile.TemporaryDirectory(prefix="surge-llama-run-") as log_dir:
        directory = Path(log_dir)
        for attempt_number in range(1, max_retries + 2):
            previous = attempts[-1] if attempts else None
            retry_hint = (
                _retry_prompt_hint(previous["failure_reason"], previous.get("projector_schema_errors"))
                if previous else ""
            )
            prompt = PROMPT_TEMPLATE.format(**prompt_values, retry_hint=retry_hint)
            prompt_path = directory / f"prompt-attempt-{attempt_number}.txt"
            log_path = directory / f"llama-completion-attempt-{attempt_number}.log"
            prompt_path.write_text(prompt, encoding="utf-8")
            command = build_completion_command(model_path, prompt_path)
            command.extend(["--log-file", str(log_path)])
            attempt_started = time.perf_counter()

            try:
                proc, timed_out = run_command(command, TIMEOUT_SECONDS)
                log_output = _read_diagnostic_log(log_path)
                stdout = proc.stdout if isinstance(proc.stdout, str) else ""
                stderr = proc.stderr if isinstance(proc.stderr, str) else ""
                return_code = proc.returncode
            except OSError as error:
                stdout, stderr, log_output = "", str(error), ""
                return_code, timed_out = None, False
                launch_error = "llama_completion_launch_failed"
            else:
                launch_error = None

            elapsed = time.perf_counter() - attempt_started
            diagnostics = "\n".join(part for part in (stderr, log_output) if part)
            execution_error = launch_error or (
                "generation_timeout" if timed_out
                else detect_execution_error(diagnostics, return_code)
            )
            streams = parse_cli_streams(stdout, diagnostics, return_code)
            parsed = streams["parsed_output"]
            schema_errors = validate_projector_output(parsed, SCHEMA_PATH) if parsed is not None else []
            evidence = validate_evidence_spans(parsed, case) if parsed is not None and not schema_errors else {
                "valid": False, "span_count": 0,
                "errors": ["generation_not_json"] if parsed is None else ["schema_validation_failed"]
            }
            failure_reason = classify_generation_failure(
                stdout, parsed, return_code, timed_out, execution_error,
                schema_errors, evidence["valid"] if parsed is not None and not schema_errors else None
            )
            rss_values = re.findall(r"Maximum resident set size \(kbytes\):\s*(\d+)", stderr)
            semantic_metrics = None
            if parsed is not None and not schema_errors:
                semantic_metrics = evaluate_case(case, parsed)

            attempt = {
                "attempt_number": attempt_number,
                "is_retry": attempt_number > 1,
                "return_code": return_code,
                "timeout": timed_out,
                "elapsed_seconds": elapsed,
                "peak_rss_kib": int(rss_values[-1]) if rss_values else None,
                "json_valid": parsed is not None,
                "projector_schema_valid": parsed is not None and not schema_errors,
                "projector_schema_errors": schema_errors,
                "failure_reason": failure_reason,
                "retryable": failure_reason in RETRYABLE_GENERATION_FAILURES,
                "decoding_constraint": "canonical-json-schema-file",
                "schema_constraint_path": str(SCHEMA_PATH),
                "generation_stream_valid": streams["generation_stream_valid"],
                "stdout_rejected": streams["stdout_rejected"],
                "raw_output": stdout[:4000],
                "parsed_output": parsed,
                "stderr": streams["diagnostic_stderr"],
                "evidence_spans_valid": evidence["valid"],
                "evidence_span_count": evidence["span_count"],
                "evidence_span_errors": evidence["errors"],
                "engineering_semantic_metrics": semantic_metrics,
            }
            attempts.append(attempt)

            # Retry only output/Schema failures. Runtime failures and timeouts remain terminal.
            if failure_reason is None or failure_reason not in RETRYABLE_GENERATION_FAILURES:
                break
            if attempt_number > max_retries:
                break

    final = attempts[-1]
    initial = attempts[0]
    retry_count = len(attempts) - 1
    retry_recovered = retry_count > 0 and final["failure_reason"] is None
    retry_exhausted = retry_count > 0 and final["failure_reason"] in RETRYABLE_GENERATION_FAILURES
    total_elapsed = time.perf_counter() - total_started
    rss_values = [item["peak_rss_kib"] for item in attempts if item.get("peak_rss_kib") is not None]

    return {
        "case_id": case["case_id"],
        "attempts": attempts,
        "attempt_count": len(attempts),
        "retry_count": retry_count,
        "retry_max_retries": max_retries,
        "retry_recovered": retry_recovered,
        "retry_exhausted": retry_exhausted,
        "retry_failure_reasons": [
            item["failure_reason"] for item in attempts if item.get("failure_reason")
        ],
        "initial_attempt_json_valid": initial["json_valid"],
        "initial_attempt_schema_valid": initial["projector_schema_valid"],
        "final_attempt_number": final["attempt_number"],
        "return_code": final["return_code"],
        "timeout": any(item["timeout"] for item in attempts),
        "elapsed_seconds": total_elapsed,
        "attempt_elapsed_seconds_total": sum(item["elapsed_seconds"] for item in attempts),
        "peak_rss_kib": max(rss_values) if rss_values else None,
        "json_valid": final["json_valid"],
        "parsed_output": final["parsed_output"],
        "generation_stream_valid": final["generation_stream_valid"],
        "stdout_rejected": final["stdout_rejected"],
        "raw_output": final["raw_output"],
        "stderr": final["stderr"],
        "decoding_constraint": final["decoding_constraint"],
        "execution_error": (
            final["failure_reason"].split(":", 1)[1]
            if isinstance(final["failure_reason"], str)
            and final["failure_reason"].startswith("execution_error:")
            else (
                "generation_timeout" if final["failure_reason"] == "generation_timeout"
                else final["failure_reason"] if final["failure_reason"] in {
                    "process_nonzero_exit", "llama_completion_launch_failed"
                } else None
            )
        ),
        "generation_failure_reason": final["failure_reason"],
        "evidence_spans_valid": final["evidence_spans_valid"],
        "evidence_span_count": final["evidence_span_count"],
        "evidence_span_errors": final["evidence_span_errors"],
        "projector_schema_valid": final["projector_schema_valid"],
        "projector_schema_errors": final["projector_schema_errors"],
        "engineering_semantic_metrics": final["engineering_semantic_metrics"],
    }

def main():
    llama_cpp_commit = validate_runtime_config()
    try:
        from huggingface_hub import hf_hub_download
    except ImportError as error:
        raise SystemExit("model matrix requires huggingface_hub; install requirements-model-matrix.txt") from error
    if not MODELS:
        raise SystemExit("model lock has no enabled candidates")
    threads = configured_threads()
    if not shutil.which(COMPLETION_CLI):
        raise SystemExit(f"llama-completion missing: {COMPLETION_CLI}")
    corpus_path = Path(os.environ.get("SURGE_GOLD_CORPUS", "tests/fixtures/semantic/gold_seed_v0.1.json"))
    if not corpus_path.is_absolute():
        corpus_path = REPO_ROOT / corpus_path
    corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
    cases = validate_matrix_corpus(corpus)
    ROOT.mkdir(parents=True, exist_ok=True)
    completion_version = subprocess.run([COMPLETION_CLI, "--version"], capture_output=True, text=True, timeout=30)
    if completion_version.returncode != 0:
        raise SystemExit("llama-completion --version failed")
    corpus_digest = sha256_file(corpus_path)
    profile_digest = hashlib.sha256(json.dumps(corpus.get("profile", {}), sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    json_max_retries = configured_json_retries()
    report = {
        "matrix": "surge-structured-projector-matrix-v1.4",
        "retry_policy": {
            "policy": "bounded-json-schema-regeneration-v1",
            "max_retries_per_case": json_max_retries,
            "retryable_failure_reasons": sorted(RETRYABLE_GENERATION_FAILURES),
            "preserve_initial_attempt": True,
            "evaluate_each_attempt_separately": True,
        },
        "corpus_id": corpus.get("corpus_id"),
        "corpus_status": "SEED_NOT_INDEPENDENT",
        "qualification_gate": "NOT_RUN",
        "expected_model_count": len(MODELS),
        "expected_case_ids": [case["case_id"] for case in cases],
        "corpus_sha256": corpus_digest, "profile_sha256": profile_digest,
        "excluded_models": EXCLUDED_MODELS,
        "experiment_manifest": {
            "manifest_version": "surge-experiment-manifest-v1.5",
            "execution_backend": "llama-completion",
            "prompt_mode": "preformatted-chatml-noninteractive",
            "prompt_transport": "utf8-temporary-file",
            "source_commit": os.environ.get("GITHUB_SHA", "unknown"),
            "llama_cpp_repository": LOCK["llama_cpp"]["repository"],
            "llama_cpp_commit": llama_cpp_commit,
            "model_lock_sha256": sha256_file(LOCK_PATH),
            "canonical_schema_sha256": sha256_file(SCHEMA_PATH),
            "llama_completion_path": str(Path(COMPLETION_CLI).resolve()),
            "runtime": {
                "python": sys.version, "platform": platform.platform(),
                "machine": platform.machine(), "processor": platform.processor(),
                "huggingface_hub": __import__("huggingface_hub").__version__,
                "llama_completion_version": (completion_version.stdout or completion_version.stderr).strip()
            },
            "threads": threads,
            "build_configuration": {
                "cmake_build_type": "Release", "ggml_native": True,
                "ggml_openmp": True, "target": "llama-completion",
            },
            "generation": {
                "max_tokens": MAX_GENERATION_TOKENS,
                "temperature": GENERATION_TEMPERATURE,
                "top_k": GENERATION_TOP_K,
                "timeout_seconds": TIMEOUT_SECONDS,
                "constrained_decoding": "canonical-json-schema-file",
                "json_max_retries": json_max_retries,
                "retry_policy": "bounded-json-schema-regeneration-v1",
                "retry_prompt_transport": "utf8-temporary-file",
                "attempt_metrics": "initial-and-each-retry-preserved",
            },
            "system_prompt_sha256": hashlib.sha256(SYSTEM.encode()).hexdigest(),
            "prompt_template_sha256": hashlib.sha256(PROMPT_TEMPLATE.encode()).hexdigest(),
            "corpus_sha256": corpus_digest, "profile_sha256": profile_digest,
            "models": [{
                "repository": m["repo"], "revision": m["revision"],
                "tokenizer_repository": m["tokenizer_repo"],
                "tokenizer_revision": m["tokenizer_revision"], "file": m["file"]
            } for m in MODELS]
        },
        "models": []
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    report["model_matrix_execution_gate"] = "NOT_RUN"
    _write_json_atomic(OUT, report)

    for model in MODELS:
        model_report = {
            "name": model["name"], "repository": model["repo"],
            "repository_revision": model["revision"],
            "tokenizer_repository": model["tokenizer_repo"],
            "tokenizer_revision": model["tokenizer_revision"],
            "tokenizer_source": "embedded GGUF tokenizer metadata; same immutable repository revision",
            "model_file": model["file"], "results": [],
        }
        report["models"].append(model_report)
        try:
            destination = ROOT / model["name"]
            destination.mkdir(parents=True, exist_ok=True)
            model_path = Path(hf_hub_download(
                repo_id=model["repo"], filename=model["file"],
                revision=model["revision"], local_dir=str(destination)
            ))
            if not model_path.is_file():
                raise OSError("download did not produce a regular model file")
            model_report.update({
                "file_bytes": model_path.stat().st_size,
                "file_sha256": sha256_file(model_path),
            })
        except Exception as error:
            model_report["execution_error"] = "model_download_or_verification_failed"
            model_report["diagnostic"] = f"{type(error).__name__}: model acquisition failed"
            _update_model_metrics(model_report)
            _write_json_atomic(OUT, report)
            break

        for case in cases:
            try:
                row = run(model, model_path, case)
            except Exception as error:
                row = {
                    "case_id": case["case_id"], "timeout": False,
                    "execution_error": "runner_internal_error",
                    "diagnostic": f"{type(error).__name__}: model case execution failed",
                    "return_code": None, "json_valid": False,
                    "projector_schema_valid": False,
                }
            model_report["results"].append(row)
            if row.get("execution_error"):
                model_report["execution_error"] = row["execution_error"]
            _update_model_metrics(model_report)
            _write_json_atomic(OUT, report)
            if row.get("execution_error"):
                break

    execution_failed = (
        len(report["models"]) != len(MODELS)
        or any(model.get("execution_error") for model in report["models"])
    )
    report["model_matrix_execution_gate"] = "FAIL" if execution_failed else "NOT_QUALIFIED"
    _write_json_atomic(OUT, report)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 1 if execution_failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
