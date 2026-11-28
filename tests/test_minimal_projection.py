import math
import unittest

from surge_adapter.minimal_projection import (
    MinimalProjectionAdapter,
    MinimalProjectionRejected,
)


class MinimalProjectionAdapterTests(unittest.TestCase):
    def setUp(self):
        self.adapter = MinimalProjectionAdapter()

    def test_existing_cluster_projection_is_preserved(self):
        value = {"information": "柴犬", "target_cluster": "cluster-dog", "distance": 100}
        result = self.adapter.validate(value)
        self.assertEqual(result.information, "柴犬")
        self.assertEqual(result.target_cluster, "cluster-dog")
        self.assertEqual(result.distance, 100.0)
        self.assertEqual(result.to_dict()["information"], "柴犬")

    def test_no_candidate_is_not_a_cluster_creation_command(self):
        value = {"information": "柴犬", "target_cluster": None, "distance": 0}
        result = self.adapter.validate(value)
        self.assertIsNone(result.target_cluster)
        self.assertEqual(result.distance, 0.0)

    def test_distance_polarity_is_not_interpreted(self):
        low = self.adapter.validate(
            {"information": "A", "target_cluster": "cluster-a", "distance": -3.5}
        )
        high = self.adapter.validate(
            {"information": "B", "target_cluster": "cluster-b", "distance": 999}
        )
        self.assertEqual(low.distance, -3.5)
        self.assertEqual(high.distance, 999.0)

    def test_non_finite_distance_is_rejected(self):
        for distance in (math.nan, math.inf, -math.inf):
            with self.subTest(distance=distance):
                with self.assertRaisesRegex(MinimalProjectionRejected, "DISTANCE_MUST_BE_FINITE"):
                    self.adapter.validate(
                        {"information": "柴犬", "target_cluster": "cluster-dog", "distance": distance}
                    )

    def test_missing_and_extra_fields_are_rejected(self):
        with self.assertRaises(MinimalProjectionRejected):
            self.adapter.validate({"information": "柴犬", "target_cluster": "cluster-dog"})
        with self.assertRaisesRegex(MinimalProjectionRejected, "SCHEMA_INVALID"):
            self.adapter.validate({
                "information": "柴犬", "target_cluster": "cluster-dog",
                "distance": 1, "attributes": ["dog"],
            })

    def test_blank_information_and_cluster_are_rejected(self):
        with self.assertRaisesRegex(MinimalProjectionRejected, "INFORMATION_MUST_NOT_BE_BLANK"):
            self.adapter.validate({"information": "  ", "target_cluster": None, "distance": 0})
        with self.assertRaisesRegex(MinimalProjectionRejected, "TARGET_CLUSTER_MUST_BE_NONEMPTY_OR_NULL"):
            self.adapter.validate({"information": "柴犬", "target_cluster": " ", "distance": 0})


if __name__ == "__main__":
    unittest.main()
