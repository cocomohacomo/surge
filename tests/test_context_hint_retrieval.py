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


    def test_graph_derived_hint_promotes_unresolved_scope_and_question(self):
        class FakeCore:
            state_version = 9
        class FakeRetriever(ContextRetriever):
            def __init__(self):
                self._core = FakeCore()
            def search(self, query, kinds=None, limit=20):
                return {
                    "state_version": 9,
                    "items": [
                        {"kind": "SCOPE_SPAN", "id": "scope-open",
                         "status": "UNRESOLVED", "score": 90},
                        {"kind": "SCOPE_SPAN", "id": "scope-closed",
                         "status": "CLOSED", "score": 88},
                        {"kind": "QUESTION_ASPECT", "id": "qa-1", "score": 80},
                        {"kind": "CLUSTER", "id": "cluster-1", "score": 70,
                         "relations": [{"kind": "ABOUT", "ref": "question-1"}]},
                        {"kind": "DEPENDENCY", "id": "dep-1", "score": 60,
                         "relations": [{"kind": "DEPENDS_ON", "ref": "claim-1"}]},
                    ],
                }
        hint = FakeRetriever().build_hint_from_graph(
            context_id="ctx", packet_ref="packet",
            source_range_digest="c" * 64, policy_version="hint-v1",
            query="予算計画", reference_budget=10,
        )
        self.assertEqual(
            hint["included_refs"],
            ["scope-open", "qa-1", "dep-1", "cluster-1", "question-1"],
        )
        self.assertNotIn("scope-closed", hint["included_refs"])

    def test_graph_derived_hint_rejects_empty_query(self):
        class FakeRetriever(ContextRetriever):
            def __init__(self):
                self._core = object()
        with self.assertRaisesRegex(ValueError, "CONTEXT_HINT_GRAPH_QUERY_INVALID"):
            FakeRetriever().build_hint_from_graph(
                context_id="ctx", packet_ref="packet",
                source_range_digest="d" * 64, policy_version="hint-v1",
                query=" ", reference_budget=5,
            )


if __name__ == "__main__":
    unittest.main()
