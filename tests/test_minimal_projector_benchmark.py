import unittest

from tools.benchmark_cpu_projectors import CASES, MAX_TOKENS, make_prompt, validate_projection


class MinimalProjectorBenchmarkTests(unittest.TestCase):
    def test_prompt_is_compact_and_contains_only_inventory_and_input(self):
        prompt = make_prompt("qwen3", CASES[0])
        self.assertIn('"information": "柴犬"', prompt)
        self.assertIn('"id": "cluster-dog"', prompt)
        self.assertIn('"id": "cluster-cat"', prompt)
        self.assertNotIn("evidence_spans", prompt)
        self.assertNotIn("attributes", prompt)
        self.assertNotIn("relations", prompt)

    def test_generation_budget_is_small(self):
        self.assertLessEqual(MAX_TOKENS, 64)

    def test_accepts_exact_minimal_projection(self):
        value = {"information": "柴犬", "target_cluster": "cluster-dog", "distance": 92}
        self.assertTrue(validate_projection(value, "柴犬", {"cluster-dog", "cluster-cat"}))

    def test_rejects_unknown_cluster_and_extra_semantics(self):
        unknown = {"information": "柴犬", "target_cluster": "cluster-unknown", "distance": 92}
        extra = {"information": "柴犬", "target_cluster": "cluster-dog", "distance": 92,
                 "attributes": []}
        self.assertFalse(validate_projection(unknown, "柴犬", {"cluster-dog", "cluster-cat"}))
        self.assertFalse(validate_projection(extra, "柴犬", {"cluster-dog", "cluster-cat"}))

    def test_rejects_invalid_distance(self):
        for distance in (-1, 101, True, 1.5):
            value = {"information": "柴犬", "target_cluster": "cluster-dog", "distance": distance}
            self.assertFalse(validate_projection(value, "柴犬", {"cluster-dog"}))

    def test_no_candidate_is_explicit_null(self):
        case = next(item for item in CASES if item["id"] == "no_candidate")
        value = {"information": case["information"], "target_cluster": None, "distance": 0}
        self.assertTrue(validate_projection(value, case["information"],
                                             {item["id"] for item in case["clusters"]}))


if __name__ == "__main__":
    unittest.main()
