import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from surge_adapter.semantic_core_bridge import SemanticCoreBridge
from surge_adapter.context_retrieval import ContextRetriever
from surge_adapter.packet_ingest import PacketRejected


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

    def test_search_all_paginates_beyond_legacy_thousand_result_cap(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "retrieval-pages.sgj"
            nodes = [
                {"id": f"cluster/page/{index:04d}", "kind": "CLUSTER",
                 "content": "common topic record", "references": []}
                for index in range(1105)
            ]
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "delta/page-seed",
                    "idempotency_key": "idem/page-seed",
                    "expected_state_version": 0,
                    "nodes": nodes,
                    "relations": [],
                })
                retriever = ContextRetriever(core)
                first = retriever.search("*", limit=1000)
                second = retriever.search("*", limit=1000, offset=1000)
                self.assertEqual(len(first["items"]), 1000)
                self.assertEqual(len(second["items"]), 105)
                self.assertIsNone(second["next_offset"])
                complete = retriever.search_all("*", page_size=300, max_items=2000)
                self.assertEqual(len(complete["items"]), 1105)
                self.assertEqual(complete["total_matches"], 1105)
                self.assertEqual(complete["state_version"], 1)

    def test_japanese_bigram_fallback_matches_intervening_particle(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "retrieval-ja.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "delta/ja-seed",
                    "idempotency_key": "idem/ja-seed",
                    "expected_state_version": 0,
                    "nodes": [{
                        "id": "cluster/ja-budget", "kind": "CLUSTER",
                        "content": "予算の配分案を検討する", "references": []
                    }],
                    "relations": [],
                })
                result = ContextRetriever(core).search("予算配分")
                self.assertEqual(result["items"][0]["id"], "cluster/ja-budget")
                self.assertLess(result["items"][0]["score"], 65)


    def test_bounded_chaos_projection_exposes_history_hypotheses_and_omissions(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "bounded-chaos-projection.sgj"
            provenance = {"producer_id": "projection-test", "producer_version": "1",
                          "policy_version": "chaos-v1", "recorded_at": "2026-11-18T01:00:00Z"}
            with SemanticCoreBridge(journal) as core:
                core.create_chaos_cluster(operation_id="create", idempotency_key="create",
                    expected_state_version=0, cluster_id="chaos/projection", provenance=provenance)
                for i in range(3):
                    core.admit_chaos_member(operation_id=f"m{i}", idempotency_key=f"m{i}",
                        expected_state_version=core.state_version, cluster_id="chaos/projection",
                        member={"member_ref": f"m{i}", "event_ref": f"event/{i}",
                                "admission_reason": "UNSTRUCTURED", "admitted_at": f"2026-11-18T01:0{i}:00Z"},
                        provenance=provenance)
                core.propose_chaos_association(operation_id="assoc", idempotency_key="assoc",
                    expected_state_version=core.state_version, cluster_id="chaos/projection",
                    association={"association_id":"a1","from_member_ref":"m0","to_member_ref":"m1",
                                 "association_type":"ANALOGY"}, provenance=provenance)
                core.review_chaos_association(operation_id="review", idempotency_key="review",
                    expected_state_version=core.state_version, cluster_id="chaos/projection",
                    association_id="a1", status="REVIEWED", provenance=provenance)
                core.propose_chaos_association(operation_id="assoc-expiring", idempotency_key="assoc-expiring",
                    expected_state_version=core.state_version, cluster_id="chaos/projection",
                    association={"association_id":"a-expiring","from_member_ref":"m0","to_member_ref":"m2",
                                 "association_type":"CO_OCCURS",
                                 "valid_from_utc":"2026-11-17T00:00:00Z",
                                 "valid_until_utc":"2026-11-18T00:00:00Z"}, provenance=provenance)
                core.expire_due_chaos_associations(operation_id="expire", idempotency_key="expire",
                    expected_state_version=core.state_version, cluster_id="chaos/projection",
                    as_of_utc="2026-11-18T00:00:00Z", provenance=provenance)
                core.add_chaos_pattern_hypothesis(operation_id="hyp", idempotency_key="hyp",
                    expected_state_version=core.state_version, cluster_id="chaos/projection",
                    hypothesis={"hypothesis_id":"h1","statement":"latent relation",
                                "member_refs":["m0","m1","m2"],"counterexample_refs":["m2"]},
                    provenance=provenance)
                full = core.get_chaos_cluster("chaos/projection")
                self.assertTrue(full["complete"])
                self.assertEqual(len(full["associations"][0]["history"]), 2)
                self.assertEqual(full["associations"][0]["history"][-1]["status"], "REVIEWED")
                expiring = next(item for item in full["associations"] if item["association_id"] == "a-expiring")
                self.assertEqual(expiring["status"], "EXPIRED")
                self.assertEqual(expiring["valid_until"], "2026-11-18T00:00:00Z")
                self.assertEqual(expiring["history"][-1]["status"], "EXPIRED")
                self.assertEqual(full["pattern_hypotheses"][0]["counterexample_refs"], ["m2"])
                bounded = core.get_chaos_cluster("chaos/projection", max_members=1,
                    max_associations=0, max_hypotheses=0)
                self.assertFalse(bounded["complete"])
                self.assertEqual(bounded["omitted"]["members"], 2)
                self.assertEqual(bounded["omitted"]["associations"], 1)
                self.assertEqual(bounded["omitted"]["pattern_hypotheses"], 1)
                with self.assertRaises(PacketRejected):
                    core.get_chaos_cluster("chaos/projection", max_members=10001)

    def test_focus_driven_chaos_retrieval_is_typed_diverse_and_restart_safe(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "focus-chaos-retrieval.sgj"
            provenance = {"producer_id": "retrieval-test", "producer_version": "1",
                          "policy_version": "chaos-v1", "recorded_at": "2026-11-18T00:00:00Z"}
            with SemanticCoreBridge(journal) as core:
                core.create_chaos_cluster(operation_id="create", idempotency_key="create",
                    expected_state_version=0, cluster_id="chaos/focus", provenance=provenance)
                for index, reason in enumerate(["A", "A", "B", "C"]):
                    core.admit_chaos_member(operation_id=f"member-{index}",
                        idempotency_key=f"member-{index}", expected_state_version=core.state_version,
                        cluster_id="chaos/focus",
                        member={"member_ref": f"m{index}", "event_ref": f"event/{index}",
                                "admission_reason": reason, "admitted_at": f"2026-11-18T00:0{index}:00Z"},
                        provenance=provenance)
                for member_ref, value in (("m0", 0.2), ("m1", 0.95)):
                    core.record_chaos_entity_observation(
                        operation_id=f"salience-{member_ref}", idempotency_key=f"salience-{member_ref}",
                        expected_state_version=core.state_version, cluster_id="chaos/focus",
                        entity_kind="MEMBER", entity_ref=member_ref,
                        observation={"value": value, "scorer_id": "retrieval-scorer",
                                     "scorer_version": "1", "policy_version": "salience-v1",
                                     "observed_at": f"2026-11-18T00:1{member_ref[-1]}:00Z"})
                core.revise_chaos_membership(
                    operation_id="remove-m3", idempotency_key="remove-m3",
                    expected_state_version=core.state_version, cluster_id="chaos/focus",
                    member_ref="m3", status="PROVISIONAL", active=False, provenance=provenance)
                core.set_chaos_focus("chaos/focus", operation_id="focus",
                    idempotency_key="focus", expected_state_version=core.state_version)
                result = ContextRetriever(core).retrieve_focus(max_members=2)
                self.assertEqual(result["target_kind"], "CHAOS_CLUSTER")
                self.assertEqual([m["member_ref"] for m in result["target"]["members"]],
                                 ["m1", "m2"])
                self.assertEqual(result["omitted_member_refs"], ["m0"])
                self.assertEqual(result["excluded_inactive_member_refs"], ["m3"])
                self.assertFalse(result["complete"])
                self.assertEqual(result["retrieval_policy"], "SCORE_AWARE_DIVERSITY_ROUND_ROBIN")
            with SemanticCoreBridge(journal) as recovered:
                result = ContextRetriever(recovered).retrieve_focus(max_members=10)
                self.assertTrue(result["complete"])
                self.assertEqual(result["target"]["cluster_id"], "chaos/focus")
                self.assertEqual(len(result["target"]["members"]), 3)
                self.assertEqual(result["excluded_inactive_member_refs"], ["m3"])

    def test_focus_retrieval_without_active_focus_is_empty(self):
        with tempfile.TemporaryDirectory() as tmp:
            with SemanticCoreBridge(pathlib.Path(tmp) / "no-focus.sgj") as core:
                result = ContextRetriever(core).retrieve_focus()
                self.assertFalse(result["found"])
                self.assertIsNone(result["target"])

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


    def test_chaos_score_observations_roundtrip_through_python_bridge(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "chaos-score-api.sgj"
            provenance = {"producer_id": "score-api-test", "producer_version": "1",
                          "policy_version": "chaos-v1", "recorded_at": "2026-11-18T00:00:00Z"}
            with SemanticCoreBridge(journal) as core:
                core.create_chaos_cluster(operation_id="create", idempotency_key="create",
                    expected_state_version=0, cluster_id="chaos/scores", provenance=provenance)
                core.admit_chaos_member(operation_id="member-add", idempotency_key="member-add",
                    expected_state_version=core.state_version, cluster_id="chaos/scores",
                    member={"member_ref": "score-member", "event_ref": "score-event",
                            "admission_reason": "HIGH_NOVELTY", "admitted_at": "2026-11-18T00:00:01Z"},
                    provenance=provenance)
                core.revise_chaos_membership(operation_id="member-remove", idempotency_key="member-remove",
                    expected_state_version=core.state_version, cluster_id="chaos/scores",
                    member_ref="score-member", status="PROVISIONAL", active=False, provenance=provenance)
                core.revise_chaos_membership(operation_id="member-restore", idempotency_key="member-restore",
                    expected_state_version=core.state_version, cluster_id="chaos/scores",
                    member_ref="score-member", status="REVIEWED", active=True, provenance=provenance)
                core.record_chaos_entity_observation(
                    operation_id="member-salience", idempotency_key="member-salience",
                    expected_state_version=core.state_version, cluster_id="chaos/scores",
                    entity_kind="MEMBER", entity_ref="score-member",
                    observation={"value": 0.73, "scorer_id": "member-scorer",
                                 "scorer_version": "1.0", "policy_version": "salience-v1",
                                 "observed_at": "2026-11-18T00:00:02Z"})
                for index, value in enumerate((0.91, 0.84)):
                    core.record_chaos_score_observation(
                        operation_id=f"score-{index}", idempotency_key=f"score-{index}",
                        expected_state_version=core.state_version, cluster_id="chaos/scores",
                        dimension="novelty",
                        observation={"value": value, "scorer_id": "recognizer",
                                     "scorer_version": "2.1", "policy_version": "score-v1",
                                     "observed_at": f"2026-11-18T00:0{index}:00Z"})
                result = core.get_chaos_cluster("chaos/scores")
                history = result["observation_history"]["novelty"]["history"]
                self.assertEqual([item["value"] for item in history], [0.91, 0.84])
                self.assertEqual(history[0]["scorer_id"], "recognizer")
                self.assertEqual(result["observations"]["novelty"]["value"], 0.84)
                self.assertEqual(result["omitted"]["score_observation_revisions"], 0)
                membership = result["members"][0]
                self.assertEqual(membership["membership_status"], "REVIEWED")
                self.assertEqual(membership["salience"]["value"], 0.73)
                self.assertEqual(membership["salience_history"]["history"][0]["scorer_id"], "member-scorer")
                self.assertEqual([item["active"] for item in membership["membership_history"]],
                                 [True, False, True])
                bounded = core.get_chaos_cluster("chaos/scores", max_history_per_association=1)
                self.assertFalse(bounded["complete"])
                self.assertEqual(bounded["observation_history"]["novelty"]["history_offset"], 1)
                self.assertEqual(bounded["omitted"]["score_observation_revisions"], 1)
                with self.assertRaises(PacketRejected):
                    core.record_chaos_score_observation(
                        operation_id="bad", idempotency_key="bad",
                        expected_state_version=core.state_version, cluster_id="chaos/scores",
                        dimension="novelty",
                        observation={"value": float("nan"), "scorer_id": "recognizer",
                                     "scorer_version": "2.1", "policy_version": "score-v1",
                                     "observed_at": "2026-11-18T00:03:00Z"})
            with SemanticCoreBridge(journal) as recovered:
                result = recovered.get_chaos_cluster("chaos/scores")
                self.assertEqual(len(result["observation_history"]["novelty"]["history"]), 2)
                self.assertEqual(result["members"][0]["membership_history"][-1]["status"], "REVIEWED")
                self.assertEqual(result["members"][0]["salience"]["value"], 0.73)


if __name__ == "__main__":
    unittest.main()
