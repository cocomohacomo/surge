import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from surge_adapter.semantic_core_bridge import SemanticCoreBridge
from surge_adapter.packet_ingest import (
    CppCoreBridge, PacketRejected, RecognitionPacketAdapter, normalize_source,
)


def make_packet(source, packet_id="bridge-p1", sequence=1, idem="bridge-i1"):
    raw = normalize_source(source)
    digest = hashlib.sha256(raw).hexdigest()
    return {
        "schema_version": "0.9.0", "packet_id": packet_id,
        "idempotency_key": idem, "context_id": "bridge-ctx",
        "source_id": "bridge-source",
        "source_position": {
            "block_ref": "block-1", "start_offset": 0, "end_offset": len(raw),
            "offset_basis": "UTF8_BYTE_NFC", "source_order": sequence,
            "source_generation": "generation-1", "source_sequence": sequence,
            "source_range_digest": digest,
        },
        "structural": {"block_type": "PARAGRAPH", "semantic_role": "MAIN_CONTENT"},
        "content": {"text": source, "normalized_digest": digest, "language": "ja"},
        "context_hint": {"state_version": 0, "temporal_mode": "CURRENT",
                         "retrieval_reason": "integration-test"},
        "coverage": {"completeness": "COMPLETE"},
        "adapter": {"adapter_id": "integration-test", "adapter_version": "1",
                    "capability_descriptor_ref": "cap-bridge"},
        "finalization": "FINAL",
    }


class PythonCppRuntimeBridgeTests(unittest.TestCase):
    def test_nfc_admission_duplicate_and_restart_replay(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "runtime.sgj"
            adapter = RecognitionPacketAdapter()
            packet = make_packet("か\u3099")
            with CppCoreBridge(journal) as core:
                first = adapter.admit_to_core(packet, "か\u3099", core)
                self.assertEqual(first.status, "COMMITTED")
                self.assertEqual(core.state_version, 1)
                self.assertEqual(core.packet_count, 1)
                duplicate = adapter.admit_to_core(packet, "か\u3099", core)
                self.assertEqual(duplicate.status, "DUPLICATE")
                self.assertEqual(core.state_version, 1)
                self.assertEqual(core.packet_count, 1)
            with CppCoreBridge(journal) as recovered:
                duplicate = adapter.admit_to_core(packet, "か\u3099", recovered)
                self.assertEqual(duplicate.status, "DUPLICATE")
                self.assertEqual(recovered.state_version, 1)
                self.assertEqual(recovered.packet_count, 1)


    def test_temporal_boundary_c_api_bridge_persists_and_restores(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "temporal-ledger.sgj"
            with SemanticCoreBridge(journal) as core:
                first = core.add_temporal_boundary(
                    transaction_id="tb-tx-1", idempotency_key="tb-idem-1",
                    event_id="tb-event-1", expected_state_version=0,
                    claim_id="claim/bridge", role="VALID_FROM",
                    valid_from="2026-04-01T00:00:00+09:00",
                    evidence_id="evidence/1", packet_id="packet/1")
                self.assertEqual(first["status"], "NEEDS_OTHER_BOUNDARY")
                self.assertEqual(core.temporal_boundary_pending_count, 1)
                second = core.add_temporal_boundary(
                    transaction_id="tb-tx-2", idempotency_key="tb-idem-2",
                    event_id="tb-event-2", expected_state_version=1,
                    claim_id="claim/bridge", role="VALID_UNTIL",
                    valid_until="2026-05-01T00:00:00+09:00",
                    evidence_id="evidence/2", packet_id="packet/2")
                self.assertEqual(second["status"], "BOUNDARIES_COMPLETE_PENDING_REVIEW")
                self.assertEqual(len(second["pending"]["evidence_ids"]), 2)
                self.assertEqual(len(second["pending"]["candidates"]), 2)
                conflict = core.add_temporal_boundary(
                    transaction_id="tb-tx-3", idempotency_key="tb-idem-3",
                    event_id="tb-event-3", expected_state_version=2,
                    claim_id="claim/bridge", role="VALID_FROM",
                    valid_from="2026-03-01T00:00:00+09:00",
                    evidence_id="evidence/3", packet_id="packet/3")
                self.assertEqual(conflict["status"], "BOUNDARY_CONFLICT_REVIEW_REQUIRED")
                self.assertEqual(core.temporal_question_count, 1)
                questions = core.get_temporal_questions("claim/bridge")
                self.assertEqual(len(questions), 1)
                self.assertEqual(questions[0]["status"], "OPEN")
                self.assertEqual(questions[0]["candidate_ids"],
                                 ["tb-event-1", "tb-event-3"])
                self.assertEqual(questions[0]["evidence_ids"],
                                 ["evidence/1", "evidence/3"])
                self.assertEqual(len(conflict["pending"]["candidates"]), 3)
                resolved = core.resolve_temporal_boundary_candidate(
                    transaction_id="tb-tx-4", idempotency_key="tb-idem-4",
                    event_id="tb-event-4", expected_state_version=5,
                    claim_id="claim/bridge", role="VALID_FROM", candidate_id="tb-event-1")
                self.assertEqual(resolved["status"], "CONFLICT_RESOLVED_PENDING_APPROVAL")
                self.assertEqual(resolved["pending"]["valid_from"], "2026-04-01T00:00:00+09:00")
                resolved_questions = core.get_temporal_questions("claim/bridge")
                self.assertEqual(resolved_questions[0]["status"], "RESOLVED_PENDING_APPROVAL")
            with SemanticCoreBridge(journal) as recovered:
                pending = recovered.get_temporal_boundary("claim/bridge")
                self.assertEqual(pending["valid_from"], "2026-04-01T00:00:00+09:00")
                self.assertEqual(pending["valid_until"], "2026-05-01T00:00:00+09:00")
                self.assertEqual(pending["packet_ids"], ["packet/1", "packet/2", "packet/3"])
                self.assertEqual(pending["status"], "CONFLICT_RESOLVED_PENDING_APPROVAL")
                self.assertEqual(len(pending["candidates"]), 3)
                restored_questions = recovered.get_temporal_questions("claim/bridge")
                self.assertEqual(restored_questions[0]["status"], "RESOLVED_PENDING_APPROVAL")
                self.assertEqual(len(restored_questions[0]["candidate_ids"]), 2)
                self.assertEqual(recovered.temporal_question_count, 1)
                self.assertEqual([c["candidate_id"] for c in pending["candidates"] if c["selected"]],
                                 ["tb-event-1", "tb-event-2"])
                cleared = recovered.clear_temporal_boundary(
                    transaction_id="tb-tx-5", idempotency_key="tb-idem-5",
                    event_id="tb-event-5", expected_state_version=7,
                    claim_id="claim/bridge")
                self.assertTrue(cleared["committed"])
                self.assertIsNone(recovered.get_temporal_boundary("claim/bridge"))

    def test_temporal_question_routes_focus_to_single_linked_cluster(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "temporal-question-focus.sgj"
            with SemanticCoreBridge(journal) as core:
                initial = core.apply_semantic_delta({
                    "delta_id": "seed-temporal-focus",
                    "idempotency_key": "seed-temporal-focus",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": "cluster/temporal", "kind": "CLUSTER", "content": "有効期間"},
                        {"id": "evidence/temporal", "kind": "EVIDENCE", "content": "公布資料"},
                        {"id": "claim/temporal", "kind": "CLAIM", "content": "施行日は4月1日",
                         "references": ["evidence/temporal"]},
                    ],
                    "relations": [
                        {"id": "about-temporal", "kind": "ABOUT",
                         "from": "claim/temporal", "to": "cluster/temporal"},
                        {"id": "supports-temporal", "kind": "SUPPORTS",
                         "from": "evidence/temporal", "to": "claim/temporal"},
                    ],
                })
                self.assertTrue(initial["committed"])
                first = core.add_temporal_boundary(
                    transaction_id="focus-tx-1", idempotency_key="focus-idem-1",
                    event_id="focus-event-1", expected_state_version=1,
                    claim_id="claim/temporal", role="VALID_FROM",
                    valid_from="2026-04-01T00:00:00+09:00",
                    evidence_id="evidence/focus-1", packet_id="packet/focus-1")
                self.assertTrue(first["committed"])
                conflict = core.add_temporal_boundary(
                    transaction_id="focus-tx-2", idempotency_key="focus-idem-2",
                    event_id="focus-event-2", expected_state_version=2,
                    claim_id="claim/temporal", role="VALID_FROM",
                    valid_from="2026-05-01T00:00:00+09:00",
                    evidence_id="evidence/focus-2", packet_id="packet/focus-2")
                self.assertEqual(conflict["status"], "BOUNDARY_CONFLICT_REVIEW_REQUIRED")
                focus = core.get_focus()
                self.assertEqual(focus["cluster_id"], "cluster/temporal")
                self.assertEqual(focus["interest_id"],
                                 "interest/temporal/question/temporal/claim/temporal/VALID_FROM/cluster/temporal")
                aspect_id = "aspect/question/temporal/claim/temporal/VALID_FROM"
                self.assertEqual(core.get_question_aspect(aspect_id)["status"], "OPEN")
                version = core.set_question_aspect_status(aspect_id, "RESOLVED")
                self.assertEqual(core.get_question_aspect(aspect_id)["status"], "RESOLVED")
                version = core.set_question_aspect_status(aspect_id, "DEFERRED", version)
                self.assertEqual(core.get_question_aspect(aspect_id)["status"], "DEFERRED")
                version = core.set_question_aspect_status(aspect_id, "OPEN", version)
                self.assertEqual(core.get_question_aspect(aspect_id)["status"], "OPEN")
                with self.assertRaisesRegex(PacketRejected, "STATE_VERSION_CONFLICT"):
                    core.set_question_aspect_status(aspect_id, "RESOLVED", version - 1)
            with SemanticCoreBridge(journal) as recovered:
                focus = recovered.get_focus()
                self.assertEqual(focus["cluster_id"], "cluster/temporal")
                questions = recovered.get_temporal_questions("claim/temporal")
                self.assertEqual(questions[0]["status"], "OPEN")
                self.assertEqual(recovered.get_question_aspect(aspect_id)["status"], "OPEN")

    def test_scoped_contradiction_reopens_question_with_new_aspect_and_preserves_scope(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "scoped-question.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "seed-scoped", "idempotency_key": "seed-scoped", "expected_state_version": 0,
                    "nodes": [
                        {"id": "cluster/scoped", "kind": "CLUSTER", "content": "制度"},
                        {"id": "evidence/scoped-a", "kind": "EVIDENCE", "content": "資料A"},
                        {"id": "evidence/scoped-b", "kind": "EVIDENCE", "content": "資料B"},
                        {"id": "claim/scoped-a", "kind": "CLAIM", "content": "期限は6月", "references": ["evidence/scoped-a"]},
                        {"id": "claim/scoped-b", "kind": "CLAIM", "content": "期限は7月", "references": ["evidence/scoped-b"]},
                    ],
                    "relations": [
                        {"id": "about/scoped-a", "kind": "ABOUT", "from": "claim/scoped-a", "to": "cluster/scoped"},
                        {"id": "about/scoped-b", "kind": "ABOUT", "from": "claim/scoped-b", "to": "cluster/scoped"},
                        {"id": "contradiction/scoped-1", "kind": "CONTRADICTS", "from": "claim/scoped-a", "to": "claim/scoped-b"},
                    ],
                })
                first = core.route_contradiction_scoped("contradiction/scoped-1", "scope/2026")
                self.assertTrue(first["committed"])
                core.set_question_aspect_status(first["question_aspect_id"], "RESOLVED")
                core.apply_semantic_delta({
                    "delta_id": "add-scoped-contradiction-2", "idempotency_key": "add-scoped-contradiction-2",
                    "expected_state_version": core.state_version,
                    "nodes": [],
                    "relations": [{"id": "contradiction/scoped-2", "kind": "CONTRADICTS", "from": "claim/scoped-b", "to": "claim/scoped-a"}],
                })
                second = core.route_contradiction_scoped("contradiction/scoped-2", "scope/2026")
                self.assertEqual(second["question_id"], first["question_id"])
                self.assertNotEqual(second["question_aspect_id"], first["question_aspect_id"])
                self.assertEqual(core.get_question_aspect(first["question_aspect_id"])["status"], "RESOLVED")
                self.assertEqual(core.get_question_aspect(second["question_aspect_id"])["status"], "OPEN")
                duplicate = core.route_contradiction_scoped("contradiction/scoped-2", "scope/2026")
                self.assertTrue(duplicate["duplicate"])
                other_scope = core.route_contradiction_scoped("contradiction/scoped-2", "scope/2027")
                self.assertNotEqual(other_scope["question_id"], first["question_id"])
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.get_question_aspect(first["question_aspect_id"])["status"], "RESOLVED")
                self.assertEqual(recovered.get_question_aspect(second["question_aspect_id"])["status"], "OPEN")
    def test_question_aggregate_state_precedence_counts_and_restart(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "question-aggregate.sgj"
            question_id = "question/aggregate"
            aspect_ids = ["aspect/aggregate/open", "aspect/aggregate/deferred",
                          "aspect/aggregate/resolved-a", "aspect/aggregate/resolved-b"]
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "seed-question-aggregate",
                    "idempotency_key": "seed-question-aggregate",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": question_id, "kind": "QUESTION", "content": "集約状態テスト"},
                        *[{"id": aspect_id, "kind": "QUESTION_ASPECT", "content": "aspect",
                           "references": [question_id]} for aspect_id in aspect_ids],
                    ],
                    "relations": [],
                })
                empty_question = "question/empty"
                core.apply_semantic_delta({
                    "delta_id": "seed-empty-question", "idempotency_key": "seed-empty-question",
                    "expected_state_version": core.state_version, "nodes": [
                        {"id": empty_question, "kind": "QUESTION", "content": "未発生"},
                    ], "relations": [],
                })
                empty = core.get_question_aggregate(empty_question)
                self.assertEqual(empty["status"], "UNKNOWN")
                self.assertEqual(empty["counts"], {"OPEN": 0, "DEFERRED": 0, "RESOLVED": 0})
                self.assertEqual(empty["aspect_ids"], [])
                with self.assertRaisesRegex(PacketRejected, "QUESTION_NOT_FOUND"):
                    core.get_question_aggregate("question/missing")
                initial_version = core.state_version
                result = core.get_question_aggregate(question_id)
                self.assertEqual(result["status"], "OPEN")
                self.assertEqual(result["counts"], {"OPEN": 4, "DEFERRED": 0, "RESOLVED": 0})
                self.assertEqual(result["state_version"], initial_version)
                self.assertEqual(core.state_version, initial_version)
                core.set_question_aspect_status(aspect_ids[0], "RESOLVED")
                core.set_question_aspect_status(aspect_ids[1], "DEFERRED")
                result = core.get_question_aggregate(question_id)
                self.assertEqual(result["status"], "OPEN")
                self.assertEqual(result["counts"], {"OPEN": 2, "DEFERRED": 1, "RESOLVED": 1})
                core.set_question_aspect_status(aspect_ids[2], "DEFERRED")
                result = core.get_question_aggregate(question_id)
                self.assertEqual(result["status"], "OPEN")
                self.assertEqual(result["counts"], {"OPEN": 1, "DEFERRED": 2, "RESOLVED": 1})
                core.set_question_aspect_status(aspect_ids[3], "DEFERRED")
                result = core.get_question_aggregate(question_id)
                self.assertEqual(result["status"], "DEFERRED")
                self.assertEqual(result["counts"], {"OPEN": 0, "DEFERRED": 3, "RESOLVED": 1})
                for aspect_id in aspect_ids:
                    core.set_question_aspect_status(aspect_id, "RESOLVED")
                result = core.get_question_aggregate(question_id)
                self.assertEqual(result["status"], "RESOLVED")
                self.assertEqual(result["counts"], {"OPEN": 0, "DEFERRED": 0, "RESOLVED": 4})
                self.assertEqual(result["aspect_ids"], sorted(aspect_ids))
                resolved_version = result["state_version"]
            with SemanticCoreBridge(journal) as recovered:
                result = recovered.get_question_aggregate(question_id)
                self.assertEqual(result["status"], "RESOLVED")
                self.assertEqual(result["state_version"], resolved_version)
                self.assertEqual(result["counts"], {"OPEN": 0, "DEFERRED": 0, "RESOLVED": 4})


    def test_question_context_bundle_restores_linked_aspects_claims_and_evidence_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            with SemanticCoreBridge(pathlib.Path(tmp) / "question-context.sgj") as core:
                core.apply_semantic_delta({
                    "delta_id": "context-bundle-seed", "idempotency_key": "context-bundle-seed",
                    "expected_state_version": 0, "relations": [],
                    "nodes": [
                        {"id": "q/root", "kind": "QUESTION", "content": "Why did the value change?"},
                        {"id": "q/child", "kind": "QUESTION", "content": "Was the input revised?"},
                        {"id": "a/root", "kind": "QUESTION_ASPECT", "content": "Temporal cause", "references": ["q/root"]},
                        {"id": "e/root", "kind": "EVIDENCE", "content": "Revision log"},
                        {"id": "e/unrelated", "kind": "EVIDENCE", "content": "Unrelated evidence"},
                        {"id": "c/root", "kind": "CLAIM", "content": "The input was revised", "references": ["e/root", "q/root"]},
                        {"id": "q/unrelated", "kind": "QUESTION", "content": "Unrelated"},
                        {"id": "c/unrelated", "kind": "CLAIM", "content": "Unrelated claim", "references": ["e/unrelated"]},
                    ],
                })
                core.apply_semantic_delta({
                    "delta_id": "context-bundle-links", "idempotency_key": "context-bundle-links",
                    "expected_state_version": core.state_version, "nodes": [],
                    "relations": [
                        {"id": "r/child-root", "kind": "DEPENDS_ON", "from": "q/child", "to": "q/root"},
                        {"id": "r/evidence-claim", "kind": "SUPPORTS", "from": "e/root", "to": "c/root"},
                    ],
                })
                before = core.state_version
                bundle = core.get_question_context_bundle("q/root")
                ids = {node["id"] for node in bundle["nodes"]}
                self.assertTrue({"q/root", "q/child", "a/root", "c/root", "e/root"}.issubset(ids))
                self.assertFalse({"q/unrelated", "c/unrelated"} & ids)
                self.assertEqual(bundle["question_id"], "q/root")
                self.assertEqual(bundle["state_version"], before)
                self.assertEqual(len(bundle["relations"]), 2)
                self.assertEqual(core.state_version, before)
                with self.assertRaisesRegex(PacketRejected, "QUESTION_CONTEXT_NODE_LIMIT"):
                    core.get_question_context_bundle("q/root", max_nodes=1)

    def test_question_reevaluation_candidates_are_bounded_read_only_and_persistent(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "question-reevaluation.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "reeval-seed", "idempotency_key": "reeval-seed",
                    "expected_state_version": 0, "relations": [],
                    "nodes": [
                        {"id": "q/root", "kind": "QUESTION", "content": "root"},
                        {"id": "q/mid", "kind": "QUESTION", "content": "middle"},
                        {"id": "q/leaf", "kind": "QUESTION", "content": "leaf"},
                        {"id": "q/other", "kind": "QUESTION", "content": "other"},
                        {"id": "a/root", "kind": "QUESTION_ASPECT", "content": "root aspect",
                         "references": ["q/root"]},
                        {"id": "a/mid", "kind": "QUESTION_ASPECT", "content": "middle aspect",
                         "references": ["q/mid"]},
                    ],
                })
                core.apply_semantic_delta({
                    "delta_id": "reeval-links", "idempotency_key": "reeval-links",
                    "expected_state_version": core.state_version, "nodes": [],
                    "relations": [
                        {"id": "r/mid-root", "kind": "DEPENDS_ON", "from": "q/mid", "to": "q/root"},
                        {"id": "r/leaf-mid", "kind": "DEPENDS_ON", "from": "q/leaf", "to": "q/mid"},
                        {"id": "r/other-root", "kind": "DEPENDS_ON", "from": "q/other", "to": "q/root"},
                    ],
                })
                before = core.state_version
                result = core.get_question_reevaluation_candidates("q/root")
                self.assertEqual([x["question_id"] for x in result["candidates"]],
                                 ["q/mid", "q/other", "q/leaf"])
                self.assertEqual([x["depth"] for x in result["candidates"]], [1, 1, 2])
                self.assertEqual(result["candidates"][0]["status"], "OPEN")
                self.assertEqual(core.state_version, before)
                shallow = core.get_question_reevaluation_candidates("q/root", max_depth=1)
                self.assertEqual([x["question_id"] for x in shallow["candidates"]],
                                 ["q/mid", "q/other"])
                with self.assertRaisesRegex(PacketRejected, "QUESTION_REEVALUATION_NODE_LIMIT"):
                    core.get_question_reevaluation_candidates("q/root", max_nodes=1)
                with self.assertRaisesRegex(PacketRejected, "QUESTION_REEVALUATION_DEPTH_INVALID"):
                    core.get_question_reevaluation_candidates("q/root", max_depth=0)
            with SemanticCoreBridge(journal) as recovered:
                again = recovered.get_question_reevaluation_candidates("q/root")
                self.assertEqual([x["question_id"] for x in again["candidates"]],
                                 ["q/mid", "q/other", "q/leaf"])
                self.assertEqual(recovered.state_version, before)


    def test_question_forgetting_policy_and_durable_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal=pathlib.Path(tmp)/"forgetting.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({"delta_id":"mem-seed","idempotency_key":"mem-seed","expected_state_version":0,"nodes":[{"id":"q/strong","kind":"QUESTION","content":"strong"},{"id":"q/weak","kind":"QUESTION","content":"weak"},{"id":"a/weak","kind":"QUESTION_ASPECT","content":"unresolved","references":["q/weak"]}],"relations":[]})
                self.assertEqual(core.recommend_question_memory(1,.8,1,.2,.5)["recommended_state"],"ACTIVE")
                self.assertEqual(core.recommend_question_memory(.05,0,0,.1,0)["recommended_state"],"DORMANT")
                before=core.state_version;core.set_question_memory_state("q/weak","DORMANT",before)
                self.assertEqual(core.get_question_memory_state("q/weak")["memory_state"],"DORMANT")
                self.assertEqual(core.get_question_aspect("a/weak")["status"],"OPEN")
                with self.assertRaisesRegex(PacketRejected,"STATE_VERSION_CONFLICT"): core.set_question_memory_state("q/weak","ARCHIVED",before)
                version=core.set_question_memory_state("q/weak","ARCHIVED")
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.get_question_memory_state("q/weak")["memory_state"],"ARCHIVED")
                self.assertEqual(recovered.get_question_aspect("a/weak")["status"],"OPEN")
                self.assertEqual(recovered.state_version,version)


    def test_question_memory_capacity_demotes_lowest_scores_atomically(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal=pathlib.Path(tmp)/"capacity.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({"delta_id":"capacity-seed","idempotency_key":"capacity-seed","expected_state_version":0,"nodes":[
                    {"id":"q/high","kind":"QUESTION","content":"high"},{"id":"q/mid","kind":"QUESTION","content":"mid"},{"id":"q/low","kind":"QUESTION","content":"low"}],"relations":[]})
                before=core.state_version
                result=core.enforce_question_memory_capacity({"q/high":.9,"q/mid":.5,"q/low":.1},2,before)
                self.assertEqual(result["demoted"],["q/low"]);self.assertEqual(result["active_after"],2)
                self.assertEqual(core.get_question_memory_state("q/low")["memory_state"],"DORMANT")
                self.assertEqual(core.get_question_memory_state("q/high")["memory_state"],"ACTIVE")
                with self.assertRaisesRegex(PacketRejected,"QUESTION_MEMORY_SCORE_MISSING"):
                    core.enforce_question_memory_capacity({"q/high":.9},1)
                with self.assertRaisesRegex(PacketRejected,"STATE_VERSION_CONFLICT"):
                    core.enforce_question_memory_capacity({"q/high":.9,"q/mid":.5},1,before)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.get_question_memory_state("q/low")["memory_state"],"DORMANT")
                self.assertEqual(recovered.get_question_memory_state("q/mid")["memory_state"],"ACTIVE")

    def test_core_derived_question_memory_plan_is_explainable_and_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "memory-plan.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "memory-plan-seed", "idempotency_key": "memory-plan-seed",
                    "expected_state_version": 0, "relations": [],
                    "nodes": [
                        {"id": "q/open", "kind": "QUESTION", "content": "open question"},
                        {"id": "q/low", "kind": "QUESTION", "content": "low priority"},
                        {"id": "q/dependent", "kind": "QUESTION", "content": "dependency protected"},
                        {"id": "a/open", "kind": "QUESTION_ASPECT", "content": "unresolved",
                         "references": ["q/open"]},
                    ],
                })
                core.apply_semantic_delta({
                    "delta_id": "memory-plan-dependency", "idempotency_key": "memory-plan-dependency",
                    "expected_state_version": core.state_version, "nodes": [],
                    "relations": [{"id": "r/protect", "kind": "DEPENDS_ON",
                                  "from": "q/dependent", "to": "q/open"}],
                })
                before = core.state_version
                plan = core.plan_question_memory_capacity(1)
                self.assertEqual(core.state_version, before)
                by_id = {item["question_id"]: item for item in plan["scores"]}
                self.assertTrue(by_id["q/dependent"]["protected_by_dependency"])
                self.assertGreaterEqual(by_id["q/dependent"]["retention_score"], 0.75)
                self.assertEqual(by_id["q/open"]["factors"]["unresolved_importance"], 1.0)
                self.assertEqual(by_id["q/low"]["factors"]["recency"], 0.5)
                self.assertIn("q/open", plan["demotion_candidates"])
                self.assertEqual(plan["state_version"], before)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version, before)
                self.assertEqual(recovered.get_question_memory_state("q/open")["memory_state"], "ACTIVE")


    def test_question_reference_history_reactivates_dormant_and_persists(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "question-reference.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "reference-seed", "idempotency_key": "reference-seed",
                    "expected_state_version": 0, "nodes": [
                        {"id": "q/ref", "kind": "QUESTION", "content": "remember me"},
                    ], "relations": [],
                })
                core.set_question_memory_state("q/ref", "DORMANT")
                first = core.record_question_reference("q/ref")
                self.assertTrue(first["reactivated"])
                self.assertEqual(first["reference_count"], 1)
                self.assertEqual(first["memory_state"], "ACTIVE")
                second = core.record_question_reference("q/ref")
                self.assertFalse(second["reactivated"])
                self.assertEqual(second["reference_count"], 2)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.get_question_memory_state("q/ref")["memory_state"], "ACTIVE")
                plan = recovered.plan_question_memory_capacity(1)
                item = next(x for x in plan["scores"] if x["question_id"] == "q/ref")
                self.assertEqual(item["factors"]["reference_frequency"], 0.4)
                self.assertEqual(recovered.state_version, second["state_version"])

    def test_dependency_aware_reference_reactivates_dormant_chain_atomically(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "dependency-reactivation.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "dependency-reactivation-seed", "idempotency_key": "dependency-reactivation-seed",
                    "expected_state_version": 0, "nodes": [
                        {"id": "q/root", "kind": "QUESTION", "content": "root"},
                        {"id": "q/mid", "kind": "QUESTION", "content": "middle"},
                        {"id": "q/leaf", "kind": "QUESTION", "content": "leaf"},
                        {"id": "q/archived", "kind": "QUESTION", "content": "archived"},
                    ], "relations": [],
                })
                core.apply_semantic_delta({
                    "delta_id": "dependency-reactivation-links", "idempotency_key": "dependency-reactivation-links",
                    "expected_state_version": core.state_version, "nodes": [],
                    "relations": [
                        {"id": "r/mid-root", "kind": "DEPENDS_ON", "from": "q/mid", "to": "q/root"},
                        {"id": "r/leaf-mid", "kind": "DEPENDS_ON", "from": "q/leaf", "to": "q/mid"},
                        {"id": "r/archive-root", "kind": "DEPENDS_ON", "from": "q/archived", "to": "q/root"},
                    ],
                })
                for question_id, state in (("q/root", "DORMANT"), ("q/mid", "DORMANT"),
                                           ("q/leaf", "DORMANT"), ("q/archived", "ARCHIVED")):
                    core.set_question_memory_state(question_id, state)
                result = core.record_question_reference_with_dependents("q/root")
                self.assertTrue(result["root_reactivated"])
                self.assertEqual(result["reactivated_dependent_count"], 2)
                self.assertEqual(core.get_question_memory_state("q/root")["memory_state"], "ACTIVE")
                self.assertEqual(core.get_question_memory_state("q/mid")["memory_state"], "ACTIVE")
                self.assertEqual(core.get_question_memory_state("q/leaf")["memory_state"], "ACTIVE")
                self.assertEqual(core.get_question_memory_state("q/archived")["memory_state"], "ARCHIVED")
                committed_version = core.state_version
            with SemanticCoreBridge(journal) as recovered:
                for question_id in ("q/root", "q/mid", "q/leaf"):
                    self.assertEqual(recovered.get_question_memory_state(question_id)["memory_state"], "ACTIVE")
                self.assertEqual(recovered.get_question_memory_state("q/archived")["memory_state"], "ARCHIVED")
                self.assertEqual(recovered.state_version, committed_version)

    def test_dependency_reactivation_node_limit_is_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            with SemanticCoreBridge(pathlib.Path(tmp) / "dependency-limit.sgj") as core:
                core.apply_semantic_delta({
                    "delta_id": "dependency-limit-seed", "idempotency_key": "dependency-limit-seed",
                    "expected_state_version": 0, "nodes": [
                        {"id": "q/root", "kind": "QUESTION", "content": "root"},
                        {"id": "q/child", "kind": "QUESTION", "content": "child"},
                        {"id": "q/child2", "kind": "QUESTION", "content": "child two"},
                    ], "relations": [],
                })
                core.apply_semantic_delta({
                    "delta_id": "dependency-limit-link", "idempotency_key": "dependency-limit-link",
                    "expected_state_version": core.state_version, "nodes": [],
                    "relations": [{"id": "r/child-root", "kind": "DEPENDS_ON", "from": "q/child", "to": "q/root"},
                        {"id": "r/child2-root", "kind": "DEPENDS_ON", "from": "q/child2", "to": "q/root"}],
                })
                before = core.state_version
                with self.assertRaisesRegex(PacketRejected, "QUESTION_REACTIVATION_NODE_LIMIT"):
                    core.record_question_reference_with_dependents("q/root", max_nodes=1)
                self.assertEqual(core.state_version, before)
                self.assertEqual(core.get_question_memory_state("q/root")["memory_state"], "ACTIVE")

    def test_archived_question_reference_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with SemanticCoreBridge(pathlib.Path(tmp) / "archived-reference.sgj") as core:
                core.apply_semantic_delta({
                    "delta_id": "archived-seed", "idempotency_key": "archived-seed",
                    "expected_state_version": 0, "nodes": [
                        {"id": "q/archive", "kind": "QUESTION", "content": "archived"},
                    ], "relations": [],
                })
                core.set_question_memory_state("q/archive", "ARCHIVED")
                with self.assertRaisesRegex(PacketRejected, "QUESTION_ARCHIVED"):
                    core.record_question_reference("q/archive")

    def test_question_relationship_network_types_cycles_and_restart(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "question-relations.sgj"
            with SemanticCoreBridge(journal) as core:
                core.apply_semantic_delta({
                    "delta_id": "question-network-seed", "idempotency_key": "question-network-seed",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": "q/a", "kind": "QUESTION", "content": "A"},
                        {"id": "q/b", "kind": "QUESTION", "content": "B"},
                        {"id": "q/c", "kind": "QUESTION", "content": "C"},
                        {"id": "cluster/x", "kind": "CLUSTER", "content": "cluster"},
                    ], "relations": [],
                })
                core.apply_semantic_delta({
                    "delta_id": "question-network-links", "idempotency_key": "question-network-links",
                    "expected_state_version": core.state_version, "nodes": [],
                    "relations": [
                        {"id": "r/contains", "kind": "CONTAINS", "from": "q/a", "to": "q/b"},
                        {"id": "r/duplicates", "kind": "DUPLICATES", "from": "q/b", "to": "q/c"},
                        {"id": "r/agrees", "kind": "AGREES_WITH", "from": "q/a", "to": "q/c"},
                        {"id": "r/conflicts", "kind": "CONFLICTS_WITH", "from": "q/b", "to": "q/c"},
                        {"id": "r/depends", "kind": "DEPENDS_ON", "from": "q/a", "to": "q/b"},
                    ],
                })
                before = core.state_version
                with self.assertRaisesRegex(PacketRejected, "QUESTION_DEPENDENCY_CYCLE"):
                    core.apply_semantic_delta({
                        "delta_id": "question-network-cycle", "idempotency_key": "question-network-cycle",
                        "expected_state_version": before, "nodes": [],
                        "relations": [
                            {"id": "r/cycle-1", "kind": "DEPENDS_ON", "from": "q/b", "to": "q/c"},
                            {"id": "r/cycle-2", "kind": "DEPENDS_ON", "from": "q/c", "to": "q/a"},
                        ],
                    })
                self.assertEqual(core.state_version, before)
                with self.assertRaisesRegex(PacketRejected, "QUESTION_RELATION_TYPE_INVALID"):
                    core.apply_semantic_delta({
                        "delta_id": "question-network-invalid-endpoint",
                        "idempotency_key": "question-network-invalid-endpoint",
                        "expected_state_version": before, "nodes": [],
                        "relations": [{"id": "r/invalid", "kind": "CONTAINS",
                                       "from": "q/a", "to": "cluster/x"}],
                    })
                self.assertEqual(core.state_version, before)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version, before)

    def test_general_contradiction_question_route_is_durable_and_idempotent(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "general-question.sgj"
            with SemanticCoreBridge(journal) as core:
                seeded = core.apply_semantic_delta({
                    "delta_id": "seed-general-question",
                    "idempotency_key": "seed-general-question",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": "cluster/general", "kind": "CLUSTER", "content": "補助金制度"},
                        {"id": "evidence/general-a", "kind": "EVIDENCE", "content": "資料A"},
                        {"id": "evidence/general-b", "kind": "EVIDENCE", "content": "資料B"},
                        {"id": "claim/general-a", "kind": "CLAIM", "content": "申請期限は6月末",
                         "references": ["evidence/general-a"]},
                        {"id": "claim/general-b", "kind": "CLAIM", "content": "申請期限は7月末",
                         "references": ["evidence/general-b"]},
                    ],
                    "relations": [
                        {"id": "about/general-a", "kind": "ABOUT",
                         "from": "claim/general-a", "to": "cluster/general"},
                        {"id": "about/general-b", "kind": "ABOUT",
                         "from": "claim/general-b", "to": "cluster/general"},
                        {"id": "contradiction/general", "kind": "CONTRADICTS",
                         "from": "claim/general-a", "to": "claim/general-b"},
                    ],
                })
                self.assertTrue(seeded["committed"])
                routed = core.route_contradiction("contradiction/general")
                self.assertTrue(routed["committed"])
                self.assertFalse(routed["duplicate"])
                self.assertTrue(routed["focus_selected"])
                self.assertEqual(len(routed["interest_ids"]), 1)
                version = core.state_version
                duplicate = core.route_contradiction("contradiction/general")
                self.assertTrue(duplicate["committed"])
                self.assertTrue(duplicate["duplicate"])
                self.assertEqual(core.state_version, version)
                self.assertEqual(core.get_focus()["cluster_id"], "cluster/general")
            with SemanticCoreBridge(journal) as recovered:
                duplicate = recovered.route_contradiction("contradiction/general")
                self.assertTrue(duplicate["duplicate"])
                self.assertEqual(recovered.get_focus()["cluster_id"], "cluster/general")
                self.assertEqual(recovered.state_version, version)

    def test_general_question_lifecycle_resolve_defer_reopen_persists(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "question-lifecycle.sgj"
            aspect_id = "aspect/contradiction/contradiction/lifecycle"
            with SemanticCoreBridge(journal) as core:
                seeded = core.apply_semantic_delta({
                    "delta_id": "seed-question-lifecycle",
                    "idempotency_key": "seed-question-lifecycle",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": "cluster/lifecycle", "kind": "CLUSTER", "content": "制度"},
                        {"id": "evidence/lifecycle-a", "kind": "EVIDENCE", "content": "資料A"},
                        {"id": "evidence/lifecycle-b", "kind": "EVIDENCE", "content": "資料B"},
                        {"id": "claim/lifecycle-a", "kind": "CLAIM", "content": "期限は6月",
                         "references": ["evidence/lifecycle-a"]},
                        {"id": "claim/lifecycle-b", "kind": "CLAIM", "content": "期限は7月",
                         "references": ["evidence/lifecycle-b"]},
                    ],
                    "relations": [
                        {"id": "about/lifecycle-a", "kind": "ABOUT",
                         "from": "claim/lifecycle-a", "to": "cluster/lifecycle"},
                        {"id": "about/lifecycle-b", "kind": "ABOUT",
                         "from": "claim/lifecycle-b", "to": "cluster/lifecycle"},
                        {"id": "contradiction/lifecycle", "kind": "CONTRADICTS",
                         "from": "claim/lifecycle-a", "to": "claim/lifecycle-b"},
                    ],
                })
                self.assertTrue(seeded["committed"])
                core.route_contradiction("contradiction/lifecycle")
                self.assertEqual(core.get_question_aspect(aspect_id)["status"], "OPEN")
                version = core.set_question_aspect_status(aspect_id, "RESOLVED")
                self.assertEqual(core.get_question_aspect(aspect_id)["status"], "RESOLVED")
                version = core.set_question_aspect_status(aspect_id, "DEFERRED", version)
                self.assertEqual(core.get_question_aspect(aspect_id)["status"], "DEFERRED")
                version = core.set_question_aspect_status(aspect_id, "OPEN", version)
                self.assertEqual(core.get_question_aspect(aspect_id)["status"], "OPEN")
                self.assertEqual(core.get_question_aspect(aspect_id)["state_version"], version)
                with self.assertRaisesRegex(PacketRejected, "STATE_VERSION_CONFLICT"):
                    core.set_question_aspect_status(aspect_id, "OPEN", version - 1)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.get_question_aspect(aspect_id)["status"], "OPEN")
                self.assertEqual(recovered.get_question_aspect(aspect_id)["state_version"],
                                 recovered.state_version)

    def test_general_contradiction_keeps_focus_unchanged_when_cluster_is_ambiguous(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "general-question-ambiguous.sgj"
            with SemanticCoreBridge(journal) as core:
                seeded = core.apply_semantic_delta({
                    "delta_id": "seed-general-question-ambiguous",
                    "idempotency_key": "seed-general-question-ambiguous",
                    "expected_state_version": 0,
                    "nodes": [
                        {"id": "cluster/general-a", "kind": "CLUSTER", "content": "制度A"},
                        {"id": "cluster/general-b", "kind": "CLUSTER", "content": "制度B"},
                        {"id": "evidence/general-a", "kind": "EVIDENCE", "content": "資料A"},
                        {"id": "evidence/general-b", "kind": "EVIDENCE", "content": "資料B"},
                        {"id": "claim/general-a", "kind": "CLAIM", "content": "期限は6月",
                         "references": ["evidence/general-a"]},
                        {"id": "claim/general-b", "kind": "CLAIM", "content": "期限は7月",
                         "references": ["evidence/general-b"]},
                    ],
                    "relations": [
                        {"id": "about/general-a", "kind": "ABOUT",
                         "from": "claim/general-a", "to": "cluster/general-a"},
                        {"id": "about/general-b", "kind": "ABOUT",
                         "from": "claim/general-b", "to": "cluster/general-b"},
                        {"id": "contradiction/general-ambiguous", "kind": "CONTRADICTS",
                         "from": "claim/general-a", "to": "claim/general-b"},
                    ],
                })
                self.assertTrue(seeded["committed"])
                routed = core.route_contradiction("contradiction/general-ambiguous")
                self.assertTrue(routed["committed"])
                self.assertFalse(routed["focus_selected"])
                self.assertEqual(len(routed["interest_ids"]), 2)
                self.assertIsNone(core.get_focus())

    def test_invalid_snapshot_rejected_before_core_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            adapter = RecognitionPacketAdapter()
            packet = make_packet("SURGE")
            with CppCoreBridge(pathlib.Path(tmp) / "runtime.sgj") as core:
                with self.assertRaisesRegex(PacketRejected, "SOURCE_SNAPSHOT_DIGEST_MISMATCH"):
                    adapter.admit_to_core(packet, "tampered", core)
                self.assertEqual(core.state_version, 0)
                self.assertEqual(core.packet_count, 0)

    def test_schema_rejection_does_not_touch_core(self):
        with tempfile.TemporaryDirectory() as tmp:
            adapter = RecognitionPacketAdapter()
            packet = make_packet("abc")
            packet["unexpected"] = True
            with CppCoreBridge(pathlib.Path(tmp) / "runtime.sgj") as core:
                with self.assertRaisesRegex(PacketRejected, "SCHEMA_INVALID"):
                    adapter.admit_to_core(packet, "abc", core)
                self.assertEqual(core.state_version, 0)


if __name__ == "__main__":
    unittest.main()
