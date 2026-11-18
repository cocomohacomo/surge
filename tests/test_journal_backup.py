import tempfile
import json
import subprocess
import unittest
import struct
import zlib
from pathlib import Path

from tools.journal_backup import create_backup, restore_backup, verify_backup, writer_lock


def _quoted(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _frame(payload):
    encoded = payload.encode("utf-8")
    return struct.pack("<I", len(encoded)) + encoded + struct.pack("<I", zlib.crc32(encoded))


def _valid_journal(commit_version=1):
    tx, idem, event_id, kind, payload = "tx-1", "idem-1", "event-1", "TEST", "payload"
    fingerprint = 1469598103934665603
    for value in (tx, "0", event_id, kind, payload):
        for byte in value.encode("utf-8"):
            fingerprint ^= byte
            fingerprint = (fingerprint * 1099511628211) & ((1 << 64) - 1)
        fingerprint ^= 0xFF
        fingerprint = (fingerprint * 1099511628211) & ((1 << 64) - 1)
    digest = f"{fingerprint:016x}"
    return (b"SURGEJ1\n" +
            _frame(f"B {_quoted(tx)} {_quoted(idem)} 0 {_quoted(digest)}") +
            _frame(f"E {_quoted(tx)} {_quoted(event_id)} {_quoted(kind)} {_quoted(payload)}") +
            _frame(f"C {_quoted(tx)} {commit_version} 1 {_quoted(digest)}"))


class JournalBackupTests(unittest.TestCase):
    def test_backup_verify_restore_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, restored = root / "live.sgj", root / "backup.sgj", root / "restored.sgj"
            source.write_bytes(_valid_journal())
            manifest_path = create_backup(source, backup)
            self.assertTrue(manifest_path.exists())
            manifest = verify_backup(backup)
            self.assertEqual(manifest["size_bytes"], source.stat().st_size)
            self.assertEqual(manifest["replay"]["state_version"], 1)
            self.assertEqual(manifest["replay"]["committed_event_count"], 1)
            restore_backup(backup, restored)
            self.assertEqual(restored.read_bytes(), source.read_bytes())

    def test_active_writer_lock_blocks_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "live.sgj"
            source.write_bytes(_valid_journal())
            with writer_lock(source):
                with self.assertRaisesRegex(RuntimeError, "JOURNAL_WRITER_LOCKED"):
                    create_backup(source, root / "blocked.sgj")

    def test_second_lock_in_same_process_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "live.sgj"
            with writer_lock(source):
                with self.assertRaisesRegex(RuntimeError, "JOURNAL_WRITER_LOCKED"):
                    with writer_lock(source):
                        self.fail("a second exclusive writer lock must not be acquired")

    def test_restore_refuses_overwrite_without_explicit_flag(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, target = root / "live.sgj", root / "backup.sgj", root / "target.sgj"
            source.write_bytes(_valid_journal())
            create_backup(source, backup)
            target.write_bytes(_valid_journal() + b"existing")
            with self.assertRaisesRegex(FileExistsError, "RESTORE_DESTINATION_EXISTS"):
                restore_backup(backup, target)
            restore_backup(backup, target, replace_existing=True)
            self.assertEqual(target.read_bytes(), source.read_bytes())

    def test_backup_rejects_semantically_invalid_journal_before_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            # All frame checksums are valid, but COMMIT advances to the wrong version.
            source.write_bytes(_valid_journal(commit_version=2))
            with self.assertRaisesRegex(ValueError, "BACKUP_JOURNAL_COMMIT_INVARIANT_FAILED"):
                create_backup(source, backup)
            self.assertFalse(backup.exists())

    def test_backup_rejects_recoverable_torn_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            source.write_bytes(_valid_journal() + b"\x01\x02")
            with self.assertRaisesRegex(ValueError, "BACKUP_JOURNAL_HAS_RECOVERABLE_TAIL"):
                create_backup(source, backup)
            self.assertFalse(backup.exists())

    def test_restored_backup_replays_with_native_cpp_eventjournal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, restored = root / "live.sgj", root / "backup.sgj", root / "restored.sgj"
            source.write_bytes(_valid_journal())
            create_backup(source, backup)
            restore_backup(backup, restored)

            repository = Path(__file__).resolve().parents[1]
            candidates = (
                repository / "build" / "surge_journal_verify",
                repository / "build" / "surge_journal_verify.exe",
                repository / "build" / "Release" / "surge_journal_verify.exe",
            )
            verifier = next((candidate for candidate in candidates if candidate.is_file()), None)
            if verifier is None:
                self.skipTest("native C++ verifier is not built in this environment")
            result = subprocess.run(
                [str(verifier), str(restored)],
                check=True, capture_output=True, text=True, timeout=15)
            report = json.loads(result.stdout)
            self.assertEqual(report["state_version"], 1)
            self.assertEqual(report["event_count"], 1)

    def test_restore_invalid_backup_preserves_existing_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, target = root / "live.sgj", root / "backup.sgj", root / "target.sgj"
            source.write_bytes(_valid_journal())
            create_backup(source, backup)
            target.write_bytes(b"untouched")
            backup.write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "BACKUP_DIGEST_OR_SIZE_MISMATCH"):
                restore_backup(backup, target, replace_existing=True)
            self.assertEqual(target.read_bytes(), b"untouched")

    def test_manifest_schema_rejects_boolean_version_and_replay_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            source.write_bytes(_valid_journal())
            manifest_path = create_backup(source, backup)
            original = json.loads(manifest_path.read_text(encoding="utf-8"))
            invalid = dict(original)
            invalid["manifest_version"] = True
            manifest_path.write_text(json.dumps(invalid), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "BACKUP_MANIFEST_VERSION_INVALID"):
                verify_backup(backup)
            invalid = dict(original)
            invalid["replay"] = {**original["replay"], "state_version": 99}
            manifest_path.write_text(json.dumps(invalid), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "BACKUP_REPLAY_METADATA_MISMATCH"):
                verify_backup(backup)

    def test_authenticated_backup_verifies_and_restores_when_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, restored, key = (
                root / "live.sgj", root / "backup.sgj",
                root / "restored.sgj", root / "backup.key")
            source.write_bytes(_valid_journal())
            key.write_bytes(b"K" * 32)
            create_backup(source, backup, key_file=key, key_id="backup-key-2026")
            manifest = verify_backup(
                backup, key_file=key, key_id="backup-key-2026", require_auth=True)
            self.assertEqual(manifest["auth"]["algorithm"], "HMAC-SHA256")
            restore_backup(
                backup, restored, key_file=key,
                key_id="backup-key-2026", require_auth=True)
            self.assertEqual(restored.read_bytes(), source.read_bytes())

    def test_authenticated_backup_rejects_missing_wrong_and_rotated_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            key, wrong_key = root / "key.bin", root / "wrong-key.bin"
            source.write_bytes(_valid_journal())
            key.write_bytes(b"A" * 32)
            wrong_key.write_bytes(b"B" * 32)
            create_backup(source, backup, key_file=key, key_id="key-v1")
            with self.assertRaisesRegex(ValueError, "BACKUP_AUTH_KEY_REQUIRED"):
                verify_backup(backup, require_auth=True)
            with self.assertRaisesRegex(ValueError, "BACKUP_AUTH_KEY_ID_MISMATCH"):
                verify_backup(backup, key_file=key, key_id="key-v2")
            with self.assertRaisesRegex(ValueError, "BACKUP_AUTH_SIGNATURE_MISMATCH"):
                verify_backup(backup, key_file=wrong_key, key_id="key-v1")

    def test_authenticated_backup_rejects_manifest_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, key = root / "live.sgj", root / "backup.sgj", root / "key.bin"
            source.write_bytes(_valid_journal())
            key.write_bytes(b"S" * 32)
            manifest_path = create_backup(source, backup, key_file=key, key_id="signing-key")
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["created_at"] = "2026-01-01T00:00:00+00:00"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "BACKUP_AUTH_SIGNATURE_MISMATCH"):
                verify_backup(backup, key_file=key, require_auth=True)
            manifest = json.loads(create_backup(
                source, root / "fresh.sgj", key_file=key, key_id="signing-key"
            ).read_text(encoding="utf-8"))
            manifest["auth"]["key_id"] = "forged-key-id"
            (root / "fresh.sgj.manifest.json").write_text(
                json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "BACKUP_AUTH_SIGNATURE_MISMATCH"):
                verify_backup(root / "fresh.sgj", key_file=key, require_auth=True)

    def test_unsigned_backup_is_rejected_when_authentication_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            source.write_bytes(_valid_journal())
            create_backup(source, backup)
            with self.assertRaisesRegex(ValueError, "BACKUP_AUTH_SIGNATURE_REQUIRED"):
                verify_backup(backup, require_auth=True)

    def test_authentication_key_must_be_at_least_32_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, key = root / "live.sgj", root / "backup.sgj", root / "key.bin"
            source.write_bytes(_valid_journal())
            key.write_bytes(b"short")
            with self.assertRaisesRegex(ValueError, "BACKUP_AUTH_KEY_TOO_SHORT"):
                create_backup(source, backup, key_file=key, key_id="weak")

    def test_backup_digest_tampering_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            source.write_bytes(_valid_journal())
            create_backup(source, backup)
            backup.write_bytes(_valid_journal() + b"tampered")
            with self.assertRaisesRegex(ValueError, "BACKUP_DIGEST_OR_SIZE_MISMATCH"):
                verify_backup(backup)


if __name__ == "__main__":
    unittest.main()
