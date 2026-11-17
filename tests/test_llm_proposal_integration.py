import json
import pathlib
import sys
import unittest
from types import SimpleNamespace

from jsonschema import Draft202012Validator

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from surge_adapter.llm_interface import LLMSemanticRuntime
from surge_adapter.proposal_builder import SCHEMA_PATH


class FixedProvider:
    model = "test-model"

    def __init__(self, *, confidence=0.95, ambiguity=0.1, kind="CLAIM", topic="予算"):
        self.confidence = confidence
        self.ambiguity = ambiguity
        self.kind = kind
        self.topic = topic

    def recognize(self, text, context=None):
        return {
            "kind": self.kind,
            "topic": self.topic,
            "proposition": "予算を増額する" if self.kind == "CLAIM" else text,
            "polarity": "POSITIVE" if self.kind == "CLAIM" else "UNKNOWN",
            "conditional": False,
            "confidence": self.confidence,
            "ambiguity": self.ambiguity,
            "importance": 0.9,
        }


class FakeCore:
    state_version = 12


class LLMProposalIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.schema = Draft202012Validator(json.loads(
            SCHEMA_PATH.read_text(encoding="utf-8")))

    def runtime_result(self, provider):
        runtime = LLMSemanticRuntime(provider, "ctx", "source", "test-generation")
        runtime.packet_adapter.admit_and_apply_to_core = (
            lambda packet, text, core, delta: (
                SimpleNamespace(status="ADMITTED"),
                {"committed": True, "duplicate": False, "state_version": 13},
            )
        )
        return runtime.process("予算を増額する。", FakeCore(),
                               evidence_provenance={
                                   "observed_at": "2026-11-17T09:00:00+09:00",
                                   "source_retrieved_at": "2026-11-17T09:05:00+09:00",
                               })

    def test_runtime_emits_schema_valid_proposal_before_core_commit(self):
        result = self.runtime_result(FixedProvider())
        proposal = result["proposal"]
        self.assertEqual(list(self.schema.iter_errors(proposal)), [])
        self.assertEqual(proposal["packet_ref"], result["packet_id"])
        self.assertEqual(proposal["base_state_version"], 12)
        self.assertEqual(proposal["labels"][0]["label"], "CLAIM")
        self.assertTrue(result["semantic_commit"]["committed"])
        self.assertTrue(result["claim_created"])
        self.assertFalse(result["review_required"])

    def test_review_proposal_does_not_commit_resolved_claim(self):
        result = self.runtime_result(FixedProvider(confidence=0.78))
        self.assertEqual(result["decision"], "REVIEW")
        self.assertTrue(result["review_required"])
        self.assertFalse(result["claim_created"])
        self.assertIn("MANUAL_REVIEW_REQUIRED", result["proposal"]["warnings"])

    def test_abstained_proposal_does_not_commit_resolved_claim(self):
        result = self.runtime_result(FixedProvider(confidence=0.4))
        self.assertEqual(result["decision"], "ABSTAIN")
        self.assertFalse(result["claim_created"])
        self.assertIn("LOW_MARGIN", result["proposal"]["abstention_reasons"])

    def test_proposal_builder_is_independent_of_topic_presence(self):
        result = self.runtime_result(FixedProvider(topic=None))
        self.assertEqual(list(self.schema.iter_errors(result["proposal"])), [])
        self.assertEqual([x["family"] for x in result["proposal"]["labels"]],
                         ["SPEECH_FUNCTION"])

    def test_claim_gate_requires_accept(self):
        runtime = LLMSemanticRuntime(FixedProvider(), "ctx", "source")
        accepted = SimpleNamespace(decision="ACCEPT")
        review = SimpleNamespace(decision="REVIEW")
        abstained = SimpleNamespace(decision="ABSTAIN")
        candidate = {"kind": "CLAIM"}
        self.assertTrue(runtime._accepts_claim(candidate, accepted))
        self.assertFalse(runtime._accepts_claim(candidate, review))
        self.assertFalse(runtime._accepts_claim(candidate, abstained))


if __name__ == "__main__":
    unittest.main()
