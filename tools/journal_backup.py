#!/usr/bin/env python3
"""Crash-conscious backup, verification and restore for a stopped SURGE journal."""
from __future__ import annotations
import argparse
import contextlib
import ctypes
import hashlib
import hmac
import json
import os
import pathlib
import re
import shutil
import sqlite3
import tempfile
import struct
from datetime import datetime, timezone

MAGIC = b"SURGEJ1\n"
IO_CHUNK_BYTES = 64 * 1024
MAX_FRAME_BYTES = 16 * 1024 * 1024
MAX_KEY_BYTES = 4096
MAX_KEYRING_BYTES = 64 * 1024
MAX_PENDING_TRANSACTIONS = 256
VALIDATION_DB_CACHE_KIB = 2048


@contextlib.contextmanager
def writer_lock(journal: pathlib.Path):
    """Acquire the same OS sidecar lock primitive used by EventJournal."""
    lock_path = pathlib.Path(str(journal) + ".lock")
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        # EventJournal uses CreateFile(..., share_mode=0). Use the identical
        # primitive here; msvcrt.locking is a different lock namespace and
        # would not exclude the C++ writer.
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        create_file = kernel32.CreateFileW
        create_file.argtypes = (
            wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
            wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
        )
        create_file.restype = wintypes.HANDLE
        close_handle = kernel32.CloseHandle
        close_handle.argtypes = (wintypes.HANDLE,)
        close_handle.restype = wintypes.BOOL
        invalid_handle = ctypes.c_void_p(-1).value
        handle = create_file(
            str(lock_path), 0xC0000000, 0, None, 4, 0x80, None
        )
        if handle is None or handle == invalid_handle:
            error = ctypes.get_last_error()
            if error in (32, 33):
                raise RuntimeError("JOURNAL_WRITER_LOCKED")
            raise OSError(error, "JOURNAL_WRITER_LOCK_ERROR", str(lock_path))
        try:
            yield
        finally:
            close_handle(handle)
        return

    fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
    acquired = False
    try:
        import fcntl
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            acquired = True
        except OSError as exc:
            raise RuntimeError("JOURNAL_WRITER_LOCKED") from exc
        yield
    finally:
        if acquired:
            fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(IO_CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fsync_directory(path: pathlib.Path) -> None:
    if os.name == "nt":
        # Windows does not expose a portable directory fsync equivalent here.
        return
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic_copy(source: pathlib.Path, destination: pathlib.Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=destination.name + ".tmp.",
                                     dir=destination.parent)
    temp = pathlib.Path(temp_name)
    try:
        with os.fdopen(fd, "wb") as output, source.open("rb") as input_stream:
            shutil.copyfileobj(input_stream, output, IO_CHUNK_BYTES)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temp, destination)
        fsync_directory(destination.parent)
    except Exception:
        try:
            temp.unlink(missing_ok=True)
        finally:
            raise


def atomic_write_json(destination: pathlib.Path, value: dict) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=destination.name + ".tmp.",
                                     dir=destination.parent, text=True)
    temp = pathlib.Path(temp_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, destination)
        fsync_directory(destination.parent)
    except Exception:
        try:
            temp.unlink(missing_ok=True)
        finally:
            raise



def _fnv1a_mix(value: int, text: str) -> int:
    mask = (1 << 64) - 1
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 1099511628211) & mask
    value ^= 0xFF
    return (value * 1099511628211) & mask


def _fnv1a_request_fingerprint(tx: str, version: int,
                               events: list[tuple[str, str, str]]) -> str:
    value = _fnv1a_mix(1469598103934665603, tx)
    value = _fnv1a_mix(value, str(version))
    for event_id, kind, payload in events:
        value = _fnv1a_mix(value, event_id)
        value = _fnv1a_mix(value, kind)
        value = _fnv1a_mix(value, payload)
    return f"{value:016x}"


def _new_fingerprint(tx: str, version: int) -> int:
    return _fnv1a_mix(_fnv1a_mix(1469598103934665603, tx), str(version))

def _parse_frame_tokens(payload: str) -> list[str]:
    """Parse the small std::quoted token grammar used by EventJournal."""
    tokens: list[str] = []
    index = 0
    size = len(payload)
    while index < size:
        while index < size and payload[index].isspace():
            index += 1
        if index >= size:
            break
        if payload[index] == '"':
            index += 1
            chars: list[str] = []
            closed = False
            while index < size:
                char = payload[index]
                index += 1
                if char == '"':
                    closed = True
                    break
                if char == "\\" and index < size:
                    following = payload[index]
                    if following in ('"', "\\"):
                        chars.append(following)
                        index += 1
                    else:
                        chars.append("\\")
                else:
                    chars.append(char)
            if not closed:
                raise ValueError("BACKUP_JOURNAL_RECORD_INVALID")
            tokens.append("".join(chars))
        else:
            start = index
            while index < size and not payload[index].isspace():
                index += 1
            tokens.append(payload[start:index])
    return tokens


def inspect_journal(path: pathlib.Path, *,
                    max_pending_transactions: int = MAX_PENDING_TRANSACTIONS) -> dict:
    """Validate a journal with bounded RAM and disk-backed uniqueness indexes."""
    if (isinstance(max_pending_transactions, bool) or
            not isinstance(max_pending_transactions, int) or
            not 1 <= max_pending_transactions <= 4096):
        raise ValueError("BACKUP_PENDING_TRANSACTION_LIMIT_INVALID")
    path = pathlib.Path(path)
    file_size = path.stat().st_size
    pending: dict[str, dict] = {}
    state_version = 0
    committed_events = 0
    committed_transactions = 0
    recoverable_start: int | None = None
    import zlib

    with tempfile.TemporaryDirectory(prefix="surge-journal-index-") as temp_dir:
        db = sqlite3.connect(str(pathlib.Path(temp_dir) / "index.sqlite3"))
        try:
            db.execute(f"PRAGMA cache_size=-{VALIDATION_DB_CACHE_KIB}")
            db.execute("PRAGMA temp_store=FILE")
            db.execute("PRAGMA journal_mode=OFF")
            db.execute("PRAGMA synchronous=OFF")
            db.executescript("""
                CREATE TABLE transaction_ids (value TEXT PRIMARY KEY) WITHOUT ROWID;
                CREATE TABLE idempotency_keys (value TEXT PRIMARY KEY) WITHOUT ROWID;
                CREATE TABLE committed_event_ids (value TEXT PRIMARY KEY) WITHOUT ROWID;
                CREATE TABLE pending_event_ids (
                    tx TEXT NOT NULL, value TEXT NOT NULL,
                    PRIMARY KEY (tx, value)
                ) WITHOUT ROWID;
            """)
            with path.open("rb") as stream:
                if stream.read(len(MAGIC)) != MAGIC:
                    raise ValueError("BACKUP_JOURNAL_HEADER_INVALID")
                while True:
                    frame_start = stream.tell()
                    length_bytes = stream.read(4)
                    if not length_bytes:
                        break
                    if len(length_bytes) != 4:
                        recoverable_start = frame_start
                        break
                    length = struct.unpack("<I", length_bytes)[0]
                    if length > MAX_FRAME_BYTES:
                        raise ValueError("BACKUP_JOURNAL_FRAME_LENGTH_INVALID")
                    raw_payload = stream.read(length)
                    if len(raw_payload) != length:
                        recoverable_start = frame_start
                        break
                    crc_bytes = stream.read(4)
                    if len(crc_bytes) != 4:
                        recoverable_start = frame_start
                        break
                    expected_crc = struct.unpack("<I", crc_bytes)[0]
                    if zlib.crc32(raw_payload) & 0xFFFFFFFF != expected_crc:
                        raise ValueError(f"BACKUP_JOURNAL_CHECKSUM_MISMATCH@{frame_start}")
                    try:
                        payload = raw_payload.decode("utf-8")
                    except UnicodeDecodeError as exc:
                        raise ValueError("BACKUP_JOURNAL_UTF8_INVALID") from exc
                    tokens = _parse_frame_tokens(payload)
                    if not tokens:
                        raise ValueError("BACKUP_JOURNAL_RECORD_INVALID")
                    record_type = tokens[0]
                    if record_type == "B":
                        if len(tokens) < 5:
                            raise ValueError("BACKUP_JOURNAL_BEGIN_RECORD_INVALID")
                        tx, idem, expected_text, fingerprint = tokens[1:5]
                        try:
                            expected = int(expected_text, 10)
                        except ValueError as exc:
                            raise ValueError("BACKUP_JOURNAL_BEGIN_RECORD_INVALID") from exc
                        if not tx or not idem or not fingerprint or tx in pending:
                            raise ValueError("BACKUP_JOURNAL_BEGIN_RECORD_INVALID")
                        if len(pending) >= max_pending_transactions:
                            raise ValueError("BACKUP_PENDING_TRANSACTION_LIMIT")
                        pending[tx] = {
                            "idem": idem, "expected": expected,
                            "fingerprint": fingerprint,
                            "fingerprint_state": _new_fingerprint(tx, expected),
                            "event_count": 0, "begin_offset": frame_start,
                            "duplicate_event_id": False,
                        }
                    elif record_type == "E":
                        if len(tokens) < 5:
                            raise ValueError("BACKUP_JOURNAL_EVENT_RECORD_INVALID")
                        tx, event_id, kind, event_payload = tokens[1:5]
                        if tx not in pending or not event_id or not kind:
                            raise ValueError("BACKUP_JOURNAL_EVENT_RECORD_INVALID")
                        item = pending[tx]
                        value = item["fingerprint_state"]
                        value = _fnv1a_mix(value, event_id)
                        value = _fnv1a_mix(value, kind)
                        item["fingerprint_state"] = _fnv1a_mix(value, event_payload)
                        item["event_count"] += 1
                        try:
                            db.execute("INSERT INTO pending_event_ids(tx, value) VALUES (?, ?)",
                                       (tx, event_id))
                        except sqlite3.IntegrityError:
                            item["duplicate_event_id"] = True
                    elif record_type == "C":
                        if len(tokens) < 5:
                            raise ValueError("BACKUP_JOURNAL_COMMIT_RECORD_INVALID")
                        tx, version_text, count_text, fingerprint = tokens[1:5]
                        if tx not in pending:
                            raise ValueError("BACKUP_JOURNAL_COMMIT_RECORD_INVALID")
                        try:
                            version, count = int(version_text, 10), int(count_text, 10)
                        except ValueError as exc:
                            raise ValueError("BACKUP_JOURNAL_COMMIT_RECORD_INVALID") from exc
                        item = pending[tx]
                        duplicate_committed_event = db.execute(
                            """SELECT 1 FROM pending_event_ids p
                               JOIN committed_event_ids c ON c.value=p.value
                               WHERE p.tx=? LIMIT 1""", (tx,)
                        ).fetchone()
                        invalid = (
                            item["expected"] != state_version or version != state_version + 1
                            or item["event_count"] != count
                            or item["fingerprint"] != fingerprint
                            or f"{item['fingerprint_state']:016x}" != fingerprint
                            or item["duplicate_event_id"]
                        )
                        if invalid or duplicate_committed_event:
                            raise ValueError("BACKUP_JOURNAL_COMMIT_INVARIANT_FAILED")
                        try:
                            db.execute("INSERT INTO transaction_ids(value) VALUES (?)", (tx,))
                            db.execute("INSERT INTO idempotency_keys(value) VALUES (?)", (item["idem"],))
                        except sqlite3.IntegrityError as exc:
                            raise ValueError("BACKUP_JOURNAL_COMMIT_INVARIANT_FAILED") from exc
                        db.execute(
                            """INSERT INTO committed_event_ids(value)
                               SELECT value FROM pending_event_ids WHERE tx=?""", (tx,)
                        )
                        db.execute("DELETE FROM pending_event_ids WHERE tx=?", (tx,))
                        committed_events += item["event_count"]
                        committed_transactions += 1
                        state_version = version
                        del pending[tx]
                    else:
                        raise ValueError("BACKUP_JOURNAL_RECORD_TYPE_INVALID")
            if pending:
                earliest = min(item["begin_offset"] for item in pending.values())
                recoverable_start = earliest if recoverable_start is None else min(
                    recoverable_start, earliest)
        finally:
            db.close()

    recoverable_tail_bytes = (file_size - recoverable_start
                              if recoverable_start is not None else 0)
    return {
        "state_version": state_version,
        "committed_transactions": committed_transactions,
        "committed_event_count": committed_events,
        "recoverable_tail_bytes": recoverable_tail_bytes,
    }

def _load_auth_key(key_file: pathlib.Path) -> bytes:
    try:
        with pathlib.Path(key_file).open("rb") as stream:
            key = stream.read(MAX_KEY_BYTES + 1)
    except OSError as exc:
        raise ValueError("BACKUP_AUTH_KEY_UNREADABLE") from exc
    if len(key) < 32:
        raise ValueError("BACKUP_AUTH_KEY_TOO_SHORT")
    if len(key) > MAX_KEY_BYTES:
        raise ValueError("BACKUP_AUTH_KEY_TOO_LARGE")
    return key


def _canonical_manifest(manifest: dict) -> bytes:
    body = dict(manifest)
    auth = body.get("auth")
    if isinstance(auth, dict):
        auth = dict(auth)
        auth.pop("hmac_sha256", None)
        body["auth"] = auth
    return json.dumps(body, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def _load_keyring(keyring_file: pathlib.Path) -> dict[str, pathlib.Path]:
    try:
        with pathlib.Path(keyring_file).open("rb") as stream:
            encoded = stream.read(MAX_KEYRING_BYTES + 1)
        if len(encoded) > MAX_KEYRING_BYTES:
            raise ValueError("BACKUP_KEYRING_TOO_LARGE")
        raw = json.loads(encoded.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("BACKUP_KEYRING_INVALID") from exc
    if not isinstance(raw, dict) or not raw:
        raise ValueError("BACKUP_KEYRING_INVALID")
    keyring: dict[str, pathlib.Path] = {}
    for key_id, key_path in raw.items():
        if (not isinstance(key_id, str) or not key_id.strip() or
            not isinstance(key_path, str) or not key_path.strip()):
            raise ValueError("BACKUP_KEYRING_ENTRY_INVALID")
        candidate = pathlib.Path(key_path)
        if not candidate.is_absolute():
            candidate = pathlib.Path(keyring_file).resolve().parent / candidate
        keyring[key_id] = candidate
    return keyring


def _validate_auth_options(key_file, key_id):
    if (key_file is None) != (key_id is None):
        raise ValueError("BACKUP_AUTH_KEY_AND_ID_REQUIRED_TOGETHER")
    if key_file is None:
        return None
    if not isinstance(key_id, str) or not key_id.strip():
        raise ValueError("BACKUP_AUTH_KEY_ID_INVALID")
    return _load_auth_key(key_file)


def create_backup(journal: pathlib.Path, backup: pathlib.Path, *,
                  key_file: pathlib.Path | None = None, key_id: str | None = None) -> pathlib.Path:
    auth_key = _validate_auth_options(key_file, key_id)
    journal, backup = journal.resolve(), backup.resolve()
    if journal == backup:
        raise ValueError("BACKUP_PATH_EQUALS_SOURCE")
    with writer_lock(journal):
        if not journal.is_file():
            raise FileNotFoundError("JOURNAL_NOT_FOUND")
        replay = inspect_journal(journal)
        if replay["recoverable_tail_bytes"]:
            raise ValueError("BACKUP_JOURNAL_HAS_RECOVERABLE_TAIL")
        atomic_copy(journal, backup)
        manifest = {
            "manifest_version": 1,
            "journal_format": "SURGEJ1",
            "source_name": journal.name,
            "backup_file": backup.name,
            "size_bytes": backup.stat().st_size,
            "sha256": sha256_file(backup),
            "replay": replay,
            "created_at": datetime.now(timezone.utc).isoformat(),
            "consistency": "writer-lock-held-during-copy",
        }
        if auth_key is not None:
            manifest["auth"] = {
                "algorithm": "HMAC-SHA256",
                "key_id": key_id,
            }
            manifest["auth"]["hmac_sha256"] = hmac.new(
                auth_key, _canonical_manifest(manifest), hashlib.sha256
            ).hexdigest()
        atomic_write_json(pathlib.Path(str(backup) + ".manifest.json"), manifest)
    return pathlib.Path(str(backup) + ".manifest.json")


def verify_backup(backup: pathlib.Path, *, key_file: pathlib.Path | None = None,
                  key_id: str | None = None, keyring_file: pathlib.Path | None = None,
                  require_auth: bool = False) -> dict:
    if key_id is not None and (not isinstance(key_id, str) or not key_id.strip()):
        raise ValueError("BACKUP_AUTH_KEY_ID_INVALID")
    if key_file is not None and keyring_file is not None:
        raise ValueError("BACKUP_AUTH_KEY_SOURCE_CONFLICT")
    backup = backup.resolve()
    manifest_path = pathlib.Path(str(backup) + ".manifest.json")
    if not backup.is_file() or not manifest_path.is_file():
        raise FileNotFoundError("BACKUP_OR_MANIFEST_NOT_FOUND")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("BACKUP_MANIFEST_INVALID") from exc
    if (not isinstance(manifest, dict) or
        isinstance(manifest.get("manifest_version"), bool) or
        manifest.get("manifest_version") != 1 or
        manifest.get("journal_format") != "SURGEJ1"):
        raise ValueError("BACKUP_MANIFEST_VERSION_INVALID")
    created_at = manifest.get("created_at")
    try:
        timestamp = datetime.fromisoformat(created_at.replace("Z", "+00:00"))
        if timestamp.tzinfo is None or timestamp.utcoffset() is None:
            raise ValueError("BACKUP_MANIFEST_TIMESTAMP_INVALID")
    except (AttributeError, TypeError, ValueError) as exc:
        raise ValueError("BACKUP_MANIFEST_TIMESTAMP_INVALID") from exc
    if (not isinstance(manifest.get("source_name"), str) or
        not manifest["source_name"] or
        manifest.get("backup_file") != backup.name or
        manifest.get("consistency") != "writer-lock-held-during-copy"):
        raise ValueError("BACKUP_MANIFEST_FIELDS_INVALID")
    digest = manifest.get("sha256")
    if (isinstance(manifest.get("size_bytes"), bool) or
        not isinstance(manifest.get("size_bytes"), int) or
        manifest.get("size_bytes") != backup.stat().st_size or
        not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest) or
        digest != sha256_file(backup)):
        raise ValueError("BACKUP_DIGEST_OR_SIZE_MISMATCH")
    replay = inspect_journal(backup)
    if replay["recoverable_tail_bytes"]:
        raise ValueError("BACKUP_JOURNAL_HAS_RECOVERABLE_TAIL")
    if manifest.get("replay") != replay:
        raise ValueError("BACKUP_REPLAY_METADATA_MISMATCH")

    auth = manifest.get("auth")
    if auth is None:
        if require_auth or key_file is not None or key_id is not None or keyring_file is not None:
            raise ValueError("BACKUP_AUTH_SIGNATURE_REQUIRED")
    else:
        if (not isinstance(auth, dict) or set(auth) != {"algorithm", "key_id", "hmac_sha256"} or
            auth.get("algorithm") != "HMAC-SHA256" or
            not isinstance(auth.get("key_id"), str) or not auth["key_id"].strip() or
            not isinstance(auth.get("hmac_sha256"), str) or
            not re.fullmatch(r"[0-9a-f]{64}", auth["hmac_sha256"])):
            raise ValueError("BACKUP_AUTH_METADATA_INVALID")
        if key_id is not None and key_id != auth["key_id"]:
            raise ValueError("BACKUP_AUTH_KEY_ID_MISMATCH")
        if keyring_file is not None:
            keyring = _load_keyring(keyring_file)
            selected_key = keyring.get(auth["key_id"])
            if selected_key is None:
                raise ValueError("BACKUP_AUTH_KEY_NOT_IN_KEYRING")
        elif key_file is not None:
            selected_key = key_file
        else:
            raise ValueError("BACKUP_AUTH_KEY_REQUIRED")
        auth_key = _load_auth_key(selected_key)
        expected = hmac.new(auth_key, _canonical_manifest(manifest),
                            hashlib.sha256).hexdigest()
        if not hmac.compare_digest(expected, auth["hmac_sha256"]):
            raise ValueError("BACKUP_AUTH_SIGNATURE_MISMATCH")
    return manifest


def restore_backup(backup: pathlib.Path, destination: pathlib.Path,
                   replace_existing: bool = False, *,
                   key_file: pathlib.Path | None = None,
                   key_id: str | None = None,
                   keyring_file: pathlib.Path | None = None,
                   require_auth: bool = False) -> None:
    backup, destination = backup.resolve(), destination.resolve()
    verify_backup(backup, key_file=key_file, key_id=key_id,
                  keyring_file=keyring_file, require_auth=require_auth)
    if backup == destination:
        raise ValueError("RESTORE_PATH_EQUALS_BACKUP")
    with writer_lock(destination):
        if destination.exists() and not replace_existing:
            raise FileExistsError("RESTORE_DESTINATION_EXISTS_USE_REPLACE")
        atomic_copy(backup, destination)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    backup = sub.add_parser("backup")
    backup.add_argument("journal", type=pathlib.Path)
    backup.add_argument("destination", type=pathlib.Path)
    backup.add_argument("--key-file", type=pathlib.Path)
    backup.add_argument("--key-id")
    verify = sub.add_parser("verify")
    verify.add_argument("backup", type=pathlib.Path)
    verify.add_argument("--key-file", type=pathlib.Path)
    verify.add_argument("--key-id")
    verify.add_argument("--keyring-file", type=pathlib.Path)
    verify.add_argument("--require-auth", action="store_true")
    restore = sub.add_parser("restore")
    restore.add_argument("backup", type=pathlib.Path)
    restore.add_argument("destination", type=pathlib.Path)
    restore.add_argument("--replace", action="store_true")
    restore.add_argument("--key-file", type=pathlib.Path)
    restore.add_argument("--key-id")
    restore.add_argument("--keyring-file", type=pathlib.Path)
    restore.add_argument("--require-auth", action="store_true")
    args = parser.parse_args()
    if args.command == "backup":
        print(create_backup(args.journal, args.destination,
                             key_file=args.key_file, key_id=args.key_id))
    elif args.command == "verify":
        print(json.dumps(verify_backup(args.backup, key_file=args.key_file,
                                       key_id=args.key_id, keyring_file=args.keyring_file,
                                       require_auth=args.require_auth),
                         ensure_ascii=False, indent=2))
    else:
        restore_backup(args.backup, args.destination, args.replace,
                       key_file=args.key_file, key_id=args.key_id,
                       keyring_file=args.keyring_file,
                       require_auth=args.require_auth)
        print("RESTORE_WRITTEN; reopen with EventJournal to validate replay")


if __name__ == "__main__":
    main()
