import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from surge_adapter.semantic_core_bridge import SemanticCoreBridge
from surge_adapter.context_retrieval import ContextRetriever


class DurableContextHintRetrievalTests(unittest.TestCase):
    def test_hint_priority_and_ticket_survive_core_restart(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "hint-ticket.sgj"
            with SemanticCoreBridge(journal) as core:
                retriever = ContextRetriever(core)
                hint = retriever.build_hint(
                    context_id="ctx-a", packet_ref="packet-a",
                    source_range_digest="a" * 64, policy_version="hint-v1",
                    state_version=core.state_version,
                    candidates=[
                        {"ref": "cluster-related", "kind": "RELATED_CLUSTER"},
                        {"ref": "dependency-required", "kind": "TRIGGERED_DEPENDENCY"},
                        {"ref": "scope-open", "kind": "OPEN_SCOPE"},
                        {"ref": "question-aspect", "kind": "REQUIRED_QUESTION_ASPECT"},
                    ],
                    reference_budget=2,
                )
                self.assertFalse(hint["context_complete"])
                self.assertEqual(hint["included_refs"], ["scope-open", "question-aspect"])
                ticket = hint["retrieval_ticket"]
                self.assertIsNotNone(ticket)
                retriever.issue_ticket(ticket)
                self.assertEqual(core.state_version, 1)
                resolved = retriever.resolve_ticket(ticket["ticket_id"], 0)
                self.assertEqual(resolved["status"], "RESOLVED")
                self.assertEqual(resolved["refs"], ["dependency-required"])
                self.assertEqual(core.state_version, 2)

            with SemanticCoreBridge(journal) as recovered:
                retriever = ContextRetriever(recovered)
                self.assertEqual(recovered.state_version, 2)
                resolved = retriever.resolve_ticket(ticket["ticket_id"], 0)
                self.assertEqual(resolved["status"], "RESOLVED")
                self.assertEqual(resolved["refs"], ["dependency-required"])

    def test_optional_truncation_does_not_issue_ticket(self):
        hint = ContextRetriever.build_hint(
            context_id="ctx", packet_ref="packet", source_range_digest="b" * 64,
            policy_version="hint-v1", state_version=3,
            candidates=[
                {"ref": "required-scope", "kind": "OPEN_SCOPE"},
                {"ref": "optional", "kind": "RELATED_CLUSTER"},
            ],
            reference_budget=1,
        )
        self.assertTrue(hint["context_complete"])
        self.assertIsNone(hint["retrieval_ticket"])


if __name__ == "__main__":
    unittest.main()
