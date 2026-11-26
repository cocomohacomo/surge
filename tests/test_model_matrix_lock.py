import json
import re
import unittest
from pathlib import Path


class ModelMatrixLockTests(unittest.TestCase):
    def test_all_model_and_tokenizer_revisions_are_pinned(self):
        lock=json.loads((Path(__file__).parents[1]/"config/model-matrix-lock-v1.0.json").read_text())
        self.assertEqual(lock["lock_version"],"surge-model-matrix-lock-v1.0")
        self.assertRegex(lock["llama_cpp"]["commit"],r"^[0-9a-f]{40}$")
        self.assertGreaterEqual(len(lock["models"]),1)
        for model in lock["models"]:
            self.assertRegex(model["revision"],r"^[0-9a-f]{7,40}$")
            self.assertRegex(model["tokenizer_revision"],r"^[0-9a-f]{7,40}$")
            self.assertTrue(model["file"])
            self.assertEqual(model["tokenizer_repo"],model["repo"])
            self.assertEqual(model["tokenizer_revision"],model["revision"])


if __name__ == "__main__":
    unittest.main()
