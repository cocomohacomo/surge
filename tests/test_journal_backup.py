import tempfile
import unittest
from pathlib import Path

from tools.journal_backup import create_backup, restore_backup, verify_backup, writer_lock


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
