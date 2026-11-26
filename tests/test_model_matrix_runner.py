import unittest

from tools.run_structured_model_matrix import extract_json


class StructuredRunnerOutputTests(unittest.TestCase):
    def test_accepts_one_complete_json_object(self):
        self.assertEqual(extract_json('{"attributes":[],"relations":[]}'),
                         {"attributes": [], "relations": []})

    def test_rejects_banner_before_json(self):
        self.assertIsNone(extract_json('llama.cpp banner\\n{"attributes":[],"relations":[]}'))

    def test_rejects_prompt_echo_with_stray_braces(self):
        self.assertIsNone(extract_json('Prompt {not-json}\\n{"attributes":[],"relations":[]}'))

    def test_rejects_markdown_fence(self):
        self.assertIsNone(extract_json('```json\\n{"attributes":[],"relations":[]}\\n```'))

    def test_rejects_multiple_objects(self):
        self.assertIsNone(extract_json('{"a":1}{"b":2}'))

    def test_rejects_non_object_json(self):
        self.assertIsNone(extract_json('[]'))


if __name__ == "__main__":
    unittest.main()
