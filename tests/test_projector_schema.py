import unittest

from tools.validate_projector_output import validate


class ProjectorSchemaTests(unittest.TestCase):
    def valid_payload(self):
        return {
            "schema_version": "surge-projector-output-v1.0",
            "attributes": [{
                "key": "deadline", "value": "水曜日", "polarity": "positive",
                "category": "temporal_update",
                "evidence_spans": [{
                    "source": "text", "start": 4, "end": 8, "quote": "水曜日"
                }]
            }],
            "relations": []
        }

    def test_canonical_payload_passes(self):
        self.assertEqual(validate(self.valid_payload()), [])

    def test_missing_schema_version_fails(self):
        payload = self.valid_payload()
        del payload["schema_version"]
        self.assertTrue(validate(payload))

    def test_missing_category_or_evidence_spans_fails(self):
        payload = self.valid_payload()
        del payload["attributes"][0]["category"]
        self.assertTrue(validate(payload))

    def test_invalid_polarity_fails(self):
        payload = self.valid_payload()
        payload["attributes"][0]["polarity"] = "maybe"
        self.assertTrue(validate(payload))

    def test_additional_properties_fail(self):
        payload = self.valid_payload()
        payload["unrecognized"] = True
        self.assertTrue(validate(payload))


if __name__ == "__main__":
    unittest.main()
