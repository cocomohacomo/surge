#!/usr/bin/env python3
"""Crash-conscious backup, verification and restore for a stopped SURGE journal."""
from __future__ import annotations
import argparse
import contextlib
import ctypes
import hashlib
import json
import os
import pathlib
import shutil
import tempfile
from datetime import datetime, timezone

MAGIC = b"SURGEJ1\n"


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
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
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
            shutil.copyfileobj(input_stream, output, 1024 * 1024)
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


def create_backup(journal: pathlib.Path, backup: pathlib.Path) -> pathlib.Path:
    journal, backup = journal.resolve(), backup.resolve()
    if journal == backup:
        raise ValueError("BACKUP_PATH_EQUALS_SOURCE")
    with writer_lock(journal):
        if not journal.is_file():
            raise FileNotFoundError("JOURNAL_NOT_FOUND")
        with journal.open("rb") as stream:
            if stream.read(len(MAGIC)) != MAGIC:
                raise ValueError("JOURNAL_HEADER_INVALID")
        atomic_copy(journal, backup)
        manifest = {
            "manifest_version": 1,
            "journal_format": "SURGEJ1",
            "source_name": journal.name,
            "backup_file": backup.name,
            "size_bytes": backup.stat().st_size,
            "sha256": sha256_file(backup),
            "created_at": datetime.now(timezone.utc).isoformat(),
            "consistency": "writer-lock-held-during-copy",
        }
        atomic_write_json(pathlib.Path(str(backup) + ".manifest.json"), manifest)
    return pathlib.Path(str(backup) + ".manifest.json")


def verify_backup(backup: pathlib.Path) -> dict:
    backup = backup.resolve()
    manifest_path = pathlib.Path(str(backup) + ".manifest.json")
    if not backup.is_file() or not manifest_path.is_file():
        raise FileNotFoundError("BACKUP_OR_MANIFEST_NOT_FOUND")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("manifest_version") != 1 or manifest.get("journal_format") != "SURGEJ1":
        raise ValueError("BACKUP_MANIFEST_VERSION_INVALID")
    with backup.open("rb") as stream:
        if stream.read(len(MAGIC)) != MAGIC:
            raise ValueError("BACKUP_JOURNAL_HEADER_INVALID")
    if (manifest.get("backup_file") != backup.name or
        manifest.get("size_bytes") != backup.stat().st_size or
        manifest.get("sha256") != sha256_file(backup)):
        raise ValueError("BACKUP_DIGEST_OR_SIZE_MISMATCH")
    return manifest


def restore_backup(backup: pathlib.Path, destination: pathlib.Path,
                   replace_existing: bool = False) -> None:
    backup, destination = backup.resolve(), destination.resolve()
    verify_backup(backup)
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
    verify = sub.add_parser("verify")
    verify.add_argument("backup", type=pathlib.Path)
    restore = sub.add_parser("restore")
    restore.add_argument("backup", type=pathlib.Path)
    restore.add_argument("destination", type=pathlib.Path)
    restore.add_argument("--replace", action="store_true")
    args = parser.parse_args()
    if args.command == "backup":
        print(create_backup(args.journal, args.destination))
    elif args.command == "verify":
        print(json.dumps(verify_backup(args.backup), ensure_ascii=False, indent=2))
    else:
        restore_backup(args.backup, args.destination, args.replace)
        print("RESTORE_WRITTEN; reopen with EventJournal to validate replay")


if __name__ == "__main__":
    main()
