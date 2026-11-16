import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from surge_adapter.semantic_core_bridge import SemanticCoreBridge
from surge_adapter.context_retrieval import ContextRetriever


class PersistentContextRetrievalTests(unittest.TestCase):
    def _seed(self, core):
        source = "budget not approved"
        digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
        scope = {
            "span_id": "scope/budget/not-approved",
            "kind": "NEGATION",
            "source_digest": digest,
            "offset_basis": "UTF8_BYTE_NFC",
            "opener": {
                "anchor_id": "anchor/not",
                "source_digest": digest,
                "offset_basis": "UTF8_BYTE_NFC",
                "start_offset": 7,
                "end_offset": 10,
                "exact_text": "not",
                "packet_ref": "packet-budget-1",
            },
            "closer": None,
            "target_refs": ["claim/budget"],
            "antecedent_refs": [],
            "attribution_ref": "",
            "modality_kind": "",
            "temporal_role": "",
            "parent_ref": "",
            "packet_refs": ["packet-budget-1"],
        }
        return core.apply_semantic_delta({
            "delta_id": "delta/budget-seed",
            "idempotency_key": "idem/budget-seed",
            "expected_state_version": core.state_version,
            "nodes": [
                {"id": "cluster/budget", "kind": "CLUSTER",
                 "content": "budget allocation", "references": []},
                {"id": "evidence/budget", "kind": "EVIDENCE",
                 "content": source, "references": []},
                {"id": "claim/budget", "kind": "CLAIM",
                 "content": '{"proposition":"budget not approved","polarity":"NEGATIVE"}',
                 "references": ["evidence/budget"]},
            ],
            "relations": [
                {"id": "about/budget", "kind": "ABOUT",
                 "from": "claim/budget", "to": "cluster/budget"}
            ],
            "scope_spans": [scope],
            "source_snapshot": source,
        })

    def test_searches_cluster_claim_and_scope_after_restart(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "retrieval.sgj"
            with SemanticCoreBridge(journal) as core:
                self._seed(core)
                self.assertEqual(core.state_version, 1)
            with SemanticCoreBridge(journal) as recovered:
                result = ContextRetriever(recovered).search("budget", limit=10)
                self.assertEqual(result["state_version"], 1)
                kinds = {item["kind"] for item in result["items"]}
                self.assertTrue({"CLUSTER", "CLAIM", "SCOPE_SPAN"}.issubset(kinds))
                claim = next(item for item in result["items"] if item["kind"] == "CLAIM")
                self.assertIn("cluster/budget", [r["ref"] for r in claim["relations"]])

    def test_kind_filter_limit_and_empty_results(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "retrieval-filter.sgj"
            with SemanticCoreBridge(journal) as core:
                self._seed(core)
            with SemanticCoreBridge(journal) as recovered:
                retriever = ContextRetriever(recovered)
                only_claims = retriever.search("budget", kinds=["CLAIM"], limit=1)
                self.assertEqual(len(only_claims["items"]), 1)
                self.assertEqual(only_claims["items"][0]["kind"], "CLAIM")
                self.assertEqual(retriever.search("unmatched-token")["items"], [])


    def test_dependency_evaluation_is_retrievable_after_restart(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "dependency-evaluation.sgj"
            with SemanticCoreBridge(journal) as core:
                result = core.apply_semantic_delta({
                    "delta_id": "delta/dependency-evaluation",
                    "idempotency_key": "idem/dependency-evaluation",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": "cluster/source", "kind": "CLUSTER",
                         "content": "approved", "references": []},
                        {"id": "cluster/target", "kind": "CLUSTER",
                         "content": "target", "references": []},
                        {"id": "dependency/approval", "kind": "DEPENDENCY",
                         "content": "CONTENT_EQUALS:approved",
                         "references": ["cluster/source", "cluster/target"]},
                        {"id": "dependency/invalid", "kind": "DEPENDENCY",
                         "content": "UNKNOWN_PREDICATE",
                         "references": ["cluster/source", "cluster/target"]},
                    ],
                    "relations": [],
                })
                self.assertTrue(result["committed"])
            with SemanticCoreBridge(journal) as recovered:
                record = ContextRetriever(recovered).dependency_evaluation(
                    "delta/dependency-evaluation")
                self.assertEqual(record["delta_id"], "delta/dependency-evaluation")
                self.assertEqual(record["triggered"], [{
                    "dependency_id": "dependency/approval",
                    "prerequisite_id": "cluster/source",
                    "dependent_id": "cluster/target",
                }])
                self.assertIn("cluster/target", record["impacted_nodes"])
                self.assertEqual(record["rejected_dependencies"],
                                 ["dependency/invalid"])
                with self.assertRaisesRegex(Exception, "DEPENDENCY_EVALUATION_NOT_FOUND"):
                    ContextRetriever(recovered).dependency_evaluation("missing-delta")

    def test_invalid_query_options_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with SemanticCoreBridge(pathlib.Path(tmp) / "invalid.sgj") as core:
                retriever = ContextRetriever(core)
                with self.assertRaisesRegex(ValueError, "RETRIEVAL_QUERY_INVALID"):
                    retriever.search(" ")
                with self.assertRaisesRegex(ValueError, "RETRIEVAL_KIND_FILTER_INVALID"):
                    retriever.search("x", kinds=["UNKNOWN"])
                with self.assertRaisesRegex(ValueError, "RETRIEVAL_LIMIT_INVALID"):
                    retriever.search("x", limit=0)


if __name__ == "__main__":
    unittest.main()
