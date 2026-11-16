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



    def test_graph_derived_hint_uses_focus_dependency_conditions_and_multihop(self):
        class FakeCore:
            state_version = 12
        class FakeRetriever(ContextRetriever):
            def __init__(self):
                self._core = FakeCore()
            def search(self, query, kinds=None, limit=20):
                return {
                    "state_version": 12,
                    "focus": {"cluster_id": "cluster-focus", "interest_id": "interest-focus"},
                    "total_matches": 8,
                    "items": [
                        {"kind": "SCOPE_SPAN", "id": "scope-open", "status": "UNRESOLVED", "score": 90},
                        {"kind": "SCOPE_SPAN", "id": "scope-closed", "status": "CLOSED", "score": 88},
                        {"kind": "QUESTION_ASPECT", "id": "qa-open", "status": "OPEN", "score": 80,
                         "references": ["question-1"], "relations": []},
                        {"kind": "QUESTION_ASPECT", "id": "qa-done", "status": "RESOLVED", "score": 99,
                         "references": ["question-1"], "relations": []},
                        {"kind": "CLUSTER", "id": "cluster-focus", "score": 70, "references": [],
                         "relations": [{"kind": "ABOUT", "ref": "claim-1"}]},
                        {"kind": "INTEREST", "id": "interest-focus", "score": 70,
                         "references": ["qa-open", "cluster-focus"], "relations": []},
                        {"kind": "CLAIM", "id": "claim-1", "score": 65,
                         "references": ["evidence-1"], "relations": [{"kind": "SUPPORTS", "ref": "claim-2"}]},
                        {"kind": "CLAIM", "id": "claim-2", "score": 60,
                         "references": ["evidence-2"], "relations": []},
                        {"kind": "EVIDENCE", "id": "evidence-1", "score": 55, "references": [], "relations": []},
                        {"kind": "EVIDENCE", "id": "evidence-2", "score": 55, "references": [], "relations": []},
                        {"kind": "DEPENDENCY", "id": "dep-1", "content": "ON_CHANGE",
                         "references": ["claim-1", "claim-2"], "score": 50, "relations": []},
                    ],
                }
        hint = FakeRetriever().build_hint_from_graph(
            context_id="ctx", packet_ref="packet",
            source_range_digest="c" * 64, policy_version="hint-v2",
            query="予算計画", reference_budget=30,
            changed_refs=["claim-1"], max_depth=3, max_nodes=64,
        )
        included = set(hint["included_refs"])
        self.assertTrue({"cluster-focus", "interest-focus", "scope-open", "qa-open",
                         "claim-1", "claim-2", "dep-1"}.issubset(included))
        self.assertNotIn("scope-closed", included)
        self.assertNotIn("qa-done", included)

    def test_dependency_condition_does_not_fire_without_trigger(self):
        dependency = {"content": "ON_CHANGE", "references": ["claim-a", "claim-b"]}
        prerequisite = {"content": "予算案", "kind": "CLAIM"}
        self.assertFalse(ContextRetriever._dependency_fires(dependency, prerequisite, set()))
        self.assertTrue(ContextRetriever._dependency_fires(
            dependency, prerequisite, {"claim-a"}))
        self.assertTrue(ContextRetriever._dependency_fires(
            {"content": "CONTENT_CONTAINS:予算", "references": ["claim-a", "claim-b"]},
            prerequisite, set()))


    def test_duplicate_candidates_merge_strongest_class_and_best_evidence(self):
        hint = ContextRetriever.build_hint(
            context_id="ctx", packet_ref="packet", source_range_digest="e" * 64,
            policy_version="hint-v1", state_version=1,
            candidates=[
                {"ref": "same-ref", "kind": "LOW_PRIORITY",
                 "source_overlap": 1, "dependency_depth": 5},
                {"ref": "other-ref", "kind": "OPEN_SCOPE",
                 "source_overlap": 50, "dependency_depth": 2},
                {"ref": "same-ref", "kind": "OPEN_SCOPE",
                 "source_overlap": 10, "dependency_depth": 4},
                {"ref": "same-ref", "kind": "OPEN_SCOPE",
                 "source_overlap": 100, "dependency_depth": 1},
            ],
            reference_budget=1,
        )
        self.assertEqual(hint["included_refs"], ["same-ref"])
        self.assertFalse(hint["context_complete"])
        self.assertEqual(hint["retrieval_ticket"]["omitted_refs"], ["other-ref"])

    def test_duplicate_candidate_metadata_must_be_nonnegative_integers(self):
        with self.assertRaisesRegex(ValueError, "CONTEXT_HINT_CANDIDATE_METADATA_INVALID"):
            ContextRetriever.build_hint(
                context_id="ctx", packet_ref="packet", source_range_digest="f" * 64,
                policy_version="hint-v1", state_version=1,
                candidates=[{"ref": "x", "kind": "OPEN_SCOPE", "source_overlap": True}],
                reference_budget=1,
            )

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


    def test_semantic_delta_derives_deterministic_dependency_change_refs(self):
        delta = {
            "nodes": [{"id": "claim-new", "kind": "CLAIM", "content": "更新"}],
            "relations": [{"id": "rel-1", "kind": "SUPPORTS",
                           "from": "claim-new", "to": "claim-old"}],
            "scope_spans": [{"span_id": "scope-1", "target_refs": ["claim-old"],
                             "antecedent_refs": ["entity-1"]}],
            "focus": {"cluster_id": "cluster-1", "interest_id": "interest-1"},
        }
        self.assertEqual(
            ContextRetriever.changed_refs_from_semantic_delta(delta),
            ["claim-new", "claim-old", "cluster-1", "entity-1",
             "interest-1", "scope-1"],
        )

    def test_build_hint_after_semantic_delta_requires_current_successful_commit(self):
        class FakeCore:
            state_version = 8
        class FakeRetriever(ContextRetriever):
            def __init__(self):
                self._core = FakeCore()
                self.seen_changed_refs = None
            def build_hint_from_graph(self, **kwargs):
                self.seen_changed_refs = kwargs["changed_refs"]
                return {"state_version": 8, "context_complete": True}
        retriever = FakeRetriever()
        delta = {"nodes": [{"id": "claim-1"}], "relations": [], "scope_spans": []}
        result = retriever.build_hint_after_semantic_delta(
            context_id="ctx", packet_ref="packet", source_range_digest="a" * 64,
            policy_version="hint-v3", query="予算", reference_budget=20,
            delta=delta, commit_result={"committed": True, "state_version": 8},
        )
        self.assertEqual(retriever.seen_changed_refs, ["claim-1"])
        self.assertEqual(result["state_version"], 8)
        with self.assertRaisesRegex(Exception, "SEMANTIC_DELTA_NOT_COMMITTED"):
            retriever.build_hint_after_semantic_delta(
                context_id="ctx", packet_ref="packet", source_range_digest="a" * 64,
                policy_version="hint-v3", query="予算", reference_budget=20,
                delta=delta, commit_result={"committed": False, "state_version": 8},
            )


if __name__ == "__main__":
    unittest.main()
