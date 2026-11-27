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
                    "source": "text", "start": 3, "end": 6, "quote": "水曜日"
                }]
            }],
            "relations": []
        }

    def test_canonical_payload_passes(self):
        self.assertEqual(validate(self.valid_payload()), [])

    def test_evidence_span_offsets_match_original_source(self):
        source = "期限は水曜日です。"
        span = self.valid_payload()["attributes"][0]["evidence_spans"][0]
        self.assertEqual(source[span["start"]:span["end"]], span["quote"])

    def test_evidence_span_rejects_wrong_source_offsets(self):
        source = "期限は水曜日です。"
        span = self.valid_payload()["attributes"][0]["evidence_spans"][0]
        span["start"], span["end"] = 4, 8
        self.assertNotEqual(source[span["start"]:span["end"]], span["quote"])

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
