import tempfile
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


def _valid_journal():
    tx, idem, event_id, kind, payload = "tx-1", "idem-1", "event-1", "TEST", "payload"
    fingerprint = 1469598103934665603
    for value in (tx, "0", event_id, kind, payload):
        for byte in value.encode("utf-8"):
            fingerprint ^= byte
            fingerprint = (fingerprint * 1099511628211) & ((1 << 64) - 1)
        fingerprint ^= 0xFF
        fingerprint = (fingerprint * 1099511628211) & ((1 << 64) - 1)
    digest = f"{fingerprint:016x}"
    return (_valid_journal() +
            _frame(f"B {_quoted(tx)} {_quoted(idem)} 0 {_quoted(digest)}") +
            _frame(f"E {_quoted(tx)} {_quoted(event_id)} {_quoted(kind)} {_quoted(payload)}") +
            _frame(f"C {_quoted(tx)} 1 1 {_quoted(digest)}"))


class JournalBackupTests(unittest.TestCase):
    def test_backup_verify_restore_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, restored = root / "live.sgj", root / "backup.sgj", root / "restored.sgj"
            source.write_bytes(b"SURGEJ1\nexample-journal-payload")
            manifest_path = create_backup(source, backup)
            self.assertTrue(manifest_path.exists())
            manifest = verify_backup(backup)
            self.assertEqual(manifest["size_bytes"], source.stat().st_size)
            restore_backup(backup, restored)
            self.assertEqual(restored.read_bytes(), source.read_bytes())

    def test_active_writer_lock_blocks_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "live.sgj"
            source.write_bytes(b"SURGEJ1\n")
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
            source.write_bytes(b"SURGEJ1\nsource")
            create_backup(source, backup)
            target.write_bytes(b"SURGEJ1\nexisting")
            with self.assertRaisesRegex(FileExistsError, "RESTORE_DESTINATION_EXISTS"):
                restore_backup(backup, target)
            restore_backup(backup, target, replace_existing=True)
            self.assertEqual(target.read_bytes(), source.read_bytes())

    def test_backup_rejects_semantically_invalid_journal_before_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            # Valid frame checksums, but an impossible State Version in COMMIT.
            valid = _valid_journal()
            bad_commit = _frame('C "tx-1" 2 1 "0000000000000000"')
            source.write_bytes(valid[:valid.rfind(_frame('C "tx-1" 1 1 "' + valid.split(b'"')[-2].decode() + '"'))] if False else
                               valid[:-len(_frame('C "tx-1" 1 1 "' + "0000000000000000" + '"'))] + bad_commit)
            with self.assertRaisesRegex(ValueError, "BACKUP_JOURNAL_COMMIT_INVARIANT_FAILED"):
                create_backup(source, backup)
            self.assertFalse(backup.exists())

    def test_backup_rejects_recoverable_torn_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            source.write_bytes(_valid_journal() + b"\\x01\\x02")
            with self.assertRaisesRegex(ValueError, "BACKUP_JOURNAL_HAS_RECOVERABLE_TAIL"):
                create_backup(source, backup)
            self.assertFalse(backup.exists())

    def test_restore_invalid_backup_preserves_existing_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup, target = root / "live.sgj", root / "backup.sgj", root / "target.sgj"
            source.write_bytes(_valid_journal())
            create_backup(source, backup)
            target.write_bytes(b"untouched")
            backup.write_bytes(_valid_journal()[:-1] + b"\\x00")
            with self.assertRaisesRegex(ValueError, "BACKUP_DIGEST_OR_SIZE_MISMATCH"):
                restore_backup(backup, target, replace_existing=True)
            self.assertEqual(target.read_bytes(), b"untouched")

    def test_backup_digest_tampering_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, backup = root / "live.sgj", root / "backup.sgj"
            source.write_bytes(b"SURGEJ1\nsource")
            create_backup(source, backup)
            backup.write_bytes(b"SURGEJ1\ntampered")
            with self.assertRaisesRegex(ValueError, "BACKUP_DIGEST_OR_SIZE_MISMATCH"):
                verify_backup(backup)


if __name__ == "__main__":
    unittest.main()
