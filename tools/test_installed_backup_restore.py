#!/usr/bin/env python3
"""Exercise backup/restore with the installed native verifier in a clean process."""
from __future__ import annotations
import json
import os
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from tools.journal_backup import create_backup, restore_backup, verify_backup


def quoted(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def frame(payload: str) -> bytes:
    encoded = payload.encode("utf-8")
    return struct.pack("<I", len(encoded)) + encoded + struct.pack("<I", zlib.crc32(encoded))


def valid_journal() -> bytes:
    tx, idem, event_id, kind, payload = "tx-clean-install", "idem-clean-install", "event-clean-install", "TEST", "clean install backup restore"
    fingerprint = 1469598103934665603
    for value in (tx, "0", event_id, kind, payload):
        for byte in value.encode("utf-8"):
            fingerprint ^= byte
            fingerprint = (fingerprint * 1099511628211) & ((1 << 64) - 1)
        fingerprint ^= 0xFF
        fingerprint = (fingerprint * 1099511628211) & ((1 << 64) - 1)
    digest = f"{fingerprint:016x}"
    return (
        b"SURGEJ1\n" +
        frame(f"B {quoted(tx)} {quoted(idem)} 0 {quoted(digest)}") +
        frame(f"E {quoted(tx)} {quoted(event_id)} {quoted(kind)} {quoted(payload)}") +
        frame(f"C {quoted(tx)} 1 1 {quoted(digest)}")
    )


def main() -> int:
    temp = os.environ.get("RUNNER_TEMP")
    if not temp:
        raise SystemExit("RUNNER_TEMP_MISSING")
    prefix = Path(temp) / "surge-stage"
    executable = prefix / "bin" / ("surge_journal_verify.exe" if os.name == "nt" else "surge_journal_verify")
    if not executable.is_file():
        raise SystemExit("INSTALLED_JOURNAL_VERIFIER_MISSING:" + str(executable))
    with tempfile.TemporaryDirectory(prefix="surge-clean-backup-") as directory:
        root = Path(directory)
        source, backup, restored = root / "live.sgj", root / "backup.sgj", root / "restored.sgj"
        source.write_bytes(valid_journal())
        create_backup(source, backup)
        manifest = verify_backup(backup)
        if manifest["replay"]["state_version"] != 1:
            raise SystemExit("BACKUP_VERIFY_STATE_VERSION_MISMATCH")
        restore_backup(backup, restored)
        result = subprocess.run(
            [str(executable), str(restored)],
            check=True, capture_output=True, text=True, timeout=20,
            cwd=str(root),
        )
        report = json.loads(result.stdout)
        if report != {"state_version": 1, "event_count": 1}:
            raise SystemExit("INSTALLED_VERIFIER_REPLAY_MISMATCH:" + result.stdout)
    print("clean-prefix backup/restore and separate-process native replay passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
