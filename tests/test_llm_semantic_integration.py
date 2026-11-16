import hashlib, pathlib, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.llm_interface import LLMSemanticRuntime, LLMRecognizer
from surge_adapter.semantic_core_bridge import SemanticCoreBridge
from surge_adapter.packet_ingest import RecognitionPacketAdapter, PacketRejected

class StubProvider:
    def __init__(self, outputs):
        self.outputs=list(outputs)
        self.contexts=[]
    def recognize(self, text, context=None):
        self.contexts.append(context or {})
        return self.outputs.pop(0)

def result(kind="CLAIM", topic="予算", proposition="予算を増額", polarity="POSITIVE",
           confidence=.96, ambiguity=.05, importance=.90, conditional=False):
    return {"kind":kind,"topic":topic,"proposition":proposition,"polarity":polarity,
            "conditional":conditional,"confidence":confidence,"ambiguity":ambiguity,
            "importance":importance}

class LLMSemanticIntegrationTests(unittest.TestCase):

    def test_temporal_candidate_binds_to_single_accepted_claim_as_proposal_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            runtime = LLMSemanticRuntime(StubProvider([result(
                topic="契約A", proposition="契約Aは有効")]), "ctx", "source")
            with SemanticCoreBridge(pathlib.Path(tmp) / "temporal-bind.sgj") as core:
                out = runtime.process("契約Aは2026年4月1日から有効", core,
                                      timezone_offset="+09:00")
                self.assertEqual(len(out["temporal_bindings"]), 1)
                binding = out["temporal_bindings"][0]
                self.assertEqual(binding["binding_status"], "BOUND_PENDING_CORE_REVIEW")
                self.assertEqual(binding["claim_ref"], "claim/" + out["packet_id"])
                self.assertEqual(binding["evidence_ref"], out["evidence_id"])
                self.assertFalse(binding["claim_validity_write"])
                self.assertEqual(core.state_version, 1)

    def test_temporal_binding_refuses_multiple_cues_and_corrections(self):
        from surge_adapter.temporal_recognition import recognize_temporal_spans
        from surge_adapter.temporal_claim_binding import bind_temporal_candidates
        source = "契約Aは2026年4月1日から有効、契約Bは2026年5月1日から有効"
        spans = recognize_temporal_spans(source, timezone_offset="+09:00")
        out = bind_temporal_candidates(source, spans, claim_id="claim/1",
                                       evidence_id="evidence/1", claim_accepted=True)
        self.assertEqual(len(out), 2)
        self.assertTrue(all(x["binding_status"] == "MULTIPLE_TEMPORAL_CUES" for x in out))

        correction = "開始日は2026年4月1日ではなく2026年3月1日から有効"
        spans = recognize_temporal_spans(correction, timezone_offset="+09:00")
        out = bind_temporal_candidates(correction, spans, claim_id="claim/2",
                                       evidence_id="evidence/2", claim_accepted=True)
        self.assertTrue(all(x["binding_status"] == "CORRECTION_REVIEW_REQUIRED" for x in out))

    def test_claim_commits_packet_evidence_claim_cluster_and_replays(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal=pathlib.Path(tmp)/"runtime.sgj"
            runtime=LLMSemanticRuntime(StubProvider([result()]),"ctx","source")
            with SemanticCoreBridge(journal) as core:
                out=runtime.process("予算を増額します。",core)
                self.assertTrue(out["semantic_commit"]["committed"])
                self.assertEqual(core.state_version,1)
                self.assertEqual(core.packet_count,1)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version,1)
                self.assertEqual(recovered.packet_count,1)

    def test_contradiction_creates_question_aspect_interest_and_relation(self):
        with tempfile.TemporaryDirectory() as tmp:
            runtime=LLMSemanticRuntime(StubProvider([
                result(), result(proposition="予算を増額",polarity="NEGATIVE")]),"ctx","source")
            with SemanticCoreBridge(pathlib.Path(tmp)/"runtime.sgj") as core:
                runtime.process("予算を増額します。",core)
                out=runtime.process("予算を増額しません。",core)
                self.assertTrue(out["semantic_commit"]["committed"])
                self.assertEqual(core.state_version,2)
                self.assertEqual(core.packet_count,2)

    def test_abstention_preserves_evidence_without_claim(self):
        with tempfile.TemporaryDirectory() as tmp:
            runtime=LLMSemanticRuntime(StubProvider([
                result(confidence=.40,ambiguity=.90)]),"ctx","source")
            with SemanticCoreBridge(pathlib.Path(tmp)/"runtime.sgj") as core:
                out=runtime.process("それは不明です。",core)
                self.assertEqual(out["decision"],"ABSTAIN")
                self.assertFalse(out["claim_created"])
                self.assertEqual(core.state_version,1)

    def test_question_creates_question_and_interest(self):
        with tempfile.TemporaryDirectory() as tmp:
            runtime=LLMSemanticRuntime(StubProvider([
                result(kind="QUESTION",proposition="なぜ予算を増額するのか",
                       polarity="UNKNOWN")]),"ctx","source")
            with SemanticCoreBridge(pathlib.Path(tmp)/"runtime.sgj") as core:
                out=runtime.process("なぜ予算を増額するのですか。",core)
                self.assertTrue(out["semantic_commit"]["committed"])

    def test_invalid_llm_output_rejected_before_semantic_commit(self):
        provider=StubProvider([{"kind":"CLAIM","unexpected":True}])
        with self.assertRaisesRegex(ValueError,"LLM_OUTPUT_KEYS_INVALID"):
            LLMRecognizer(provider).recognize("任意の文章")

    def test_llm_receives_recent_context_and_current_context_hint_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            provider=StubProvider([result(), result(kind="ACKNOWLEDGEMENT",topic=None,
                proposition=None,polarity="UNKNOWN",importance=.60)])
            runtime=LLMSemanticRuntime(provider,"ctx","source")
            with SemanticCoreBridge(pathlib.Path(tmp)/"context.sgj") as core:
                runtime.process("予算を増額します。",core)
                runtime.process("うん",core)
                self.assertEqual(provider.contexts[1]["active_topic"],"予算")
                self.assertEqual(len(provider.contexts[1]["recent_claims"]),1)

    def test_typed_scope_span_is_validated_persisted_and_replayed(self):
        with tempfile.TemporaryDirectory() as tmp:
            source="not approved"
            digest=hashlib.sha256(source.encode("utf-8")).hexdigest()
            scope={"span_id":"scope/negation/1","kind":"NEGATION",
                "source_digest":digest,"offset_basis":"UTF8_BYTE_NFC",
                "opener":{"anchor_id":"anchor/not","source_digest":digest,
                    "offset_basis":"UTF8_BYTE_NFC","start_offset":0,"end_offset":3,
                    "exact_text":"not","packet_ref":"packet-1"},
                "closer":None,"target_refs":["claim-1"],"antecedent_refs":[],
                "attribution_ref":"","modality_kind":"","temporal_role":"",
                "parent_ref":"","packet_refs":["packet-1"]}
            journal=pathlib.Path(tmp)/"scopes.sgj"
            with SemanticCoreBridge(journal) as core:
                out=core.apply_semantic_delta({"delta_id":"scope-delta-1",
                    "idempotency_key":"scope-idem-1","expected_state_version":0,
                    "nodes":[{"id":"evidence-1","kind":"EVIDENCE","content":source,"references":[]},
                             {"id":"claim-1","kind":"CLAIM","content":"approved","references":["evidence-1"]}],
                    "relations":[],"scope_spans":[scope],"source_snapshot":source})
                self.assertTrue(out["committed"])
                self.assertEqual(core.scope_count,1)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.scope_count,1)
                self.assertEqual(recovered.state_version,1)

    def test_invalid_scope_anchor_rejected_without_partial_graph_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            source="not approved"
            digest=hashlib.sha256(source.encode("utf-8")).hexdigest()
            scope={"span_id":"scope/bad/1","kind":"NEGATION",
                "source_digest":digest,"offset_basis":"UTF8_BYTE_NFC",
                "opener":{"anchor_id":"anchor/not","source_digest":digest,
                    "offset_basis":"UTF8_BYTE_NFC","start_offset":0,"end_offset":3,
                    "exact_text":"wrong","packet_ref":"packet-1"},
                "closer":None,"target_refs":["claim-1"],"antecedent_refs":[],
                "attribution_ref":"","modality_kind":"","temporal_role":"",
                "parent_ref":"","packet_refs":["packet-1"]}
            with SemanticCoreBridge(pathlib.Path(tmp)/"invalid.sgj") as core:
                with self.assertRaisesRegex(Exception,"SCOPE_OPENER_ANCHOR_INVALID"):
                    core.apply_semantic_delta({"delta_id":"bad-scope-delta",
                        "idempotency_key":"bad-scope-idem","expected_state_version":0,
                        "nodes":[{"id":"evidence-1","kind":"EVIDENCE","content":source,"references":[]},
                                 {"id":"claim-1","kind":"CLAIM","content":"approved","references":["evidence-1"]}],
                        "relations":[],"scope_spans":[scope],"source_snapshot":source})
                self.assertEqual(core.state_version,0)
                self.assertEqual(core.scope_count,0)


    def test_dependency_evaluation_is_exposed_and_replayed(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "dependency-api.sgj"
            delta = {
                "delta_id": "dependency-api-delta",
                "idempotency_key": "dependency-api-idem",
                "expected_state_version": 0,
                "nodes": [
                    {"id": "prerequisite", "kind": "CLUSTER", "content": "ready", "references": []},
                    {"id": "dependent", "kind": "CLUSTER", "content": "pending", "references": []},
                    {"id": "dependency", "kind": "DEPENDENCY",
                     "content": "CONTENT_EQUALS:ready",
                     "references": ["prerequisite", "dependent"]}
                ],
                "relations": []
            }
            with SemanticCoreBridge(journal) as core:
                committed = core.apply_semantic_delta(delta)
                self.assertTrue(committed["committed"])
                record = core.dependency_evaluation("dependency-api-delta")
                self.assertEqual(record["delta_id"], "dependency-api-delta")
                self.assertEqual(record["triggered"], [{
                    "dependency_id": "dependency",
                    "prerequisite_id": "prerequisite",
                    "dependent_id": "dependent"
                }])
                self.assertIn("dependent", record["impacted_nodes"])
                self.assertEqual(record["rejected_dependencies"], [])
            with SemanticCoreBridge(journal) as recovered:
                replayed = recovered.dependency_evaluation("dependency-api-delta")
                self.assertEqual(replayed["triggered"][0]["dependency_id"], "dependency")
                self.assertEqual(recovered.state_version, 1)

    def _atomic_scope_case(self, core, source, exact_text):
        packet = LLMSemanticRuntime._packet(source, "ctx-scope", "source-scope", "scope-gen", 1)
        packet_id = packet["packet_id"]
        digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
        delta = {
            "delta_id": "semantic/" + packet_id,
            "idempotency_key": "semantic-idem/" + packet_id,
            "expected_state_version": core.state_version,
            "nodes": [
                {"id": "evidence/" + packet_id, "kind": "EVIDENCE",
                 "content": source, "references": []},
                {"id": "claim/" + packet_id, "kind": "CLAIM",
                 "content": "承認した", "references": ["evidence/" + packet_id]}
            ],
            "relations": [],
            "scope_spans": [{
                "span_id": "scope/" + packet_id, "kind": "NEGATION",
                "source_digest": digest, "offset_basis": "UTF8_BYTE_NFC",
                "opener": {"anchor_id": "anchor/" + packet_id,
                    "source_digest": digest, "offset_basis": "UTF8_BYTE_NFC",
                    "start_offset": 0, "end_offset": 3,
                    "exact_text": exact_text, "packet_ref": packet_id},
                "closer": None, "target_refs": ["claim/" + packet_id],
                "antecedent_refs": [], "attribution_ref": "",
                "modality_kind": "", "temporal_role": "", "parent_ref": "",
                "packet_refs": [packet_id]
            }]
        }
        return RecognitionPacketAdapter().admit_and_apply_to_core(packet, source, core, delta)

    def test_scope_span_commits_atomically_with_packet_and_replays(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "atomic-scope.sgj"
            with SemanticCoreBridge(journal) as core:
                admission, committed = self._atomic_scope_case(core, "not approved", "not")
                self.assertEqual(admission.status, "COMMITTED")
                self.assertTrue(committed["committed"])
                self.assertEqual(core.state_version, 1)
                self.assertEqual(core.packet_count, 1)
                self.assertEqual(core.scope_count, 1)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version, 1)
                self.assertEqual(recovered.packet_count, 1)
                self.assertEqual(recovered.scope_count, 1)

    def test_invalid_scope_prevents_packet_and_semantic_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "atomic-invalid-scope.sgj"
            with SemanticCoreBridge(journal) as core:
                with self.assertRaisesRegex(PacketRejected, "SCOPE_OPENER_ANCHOR_INVALID"):
                    self._atomic_scope_case(core, "not approved", "bad")
                self.assertEqual(core.state_version, 0)
                self.assertEqual(core.packet_count, 0)
                self.assertEqual(core.scope_count, 0)


    def test_temporal_spans_use_exact_utf8_byte_offsets(self):
        from surge_adapter.temporal_recognition import recognize_temporal_spans
        source = "契約は2026年4月1日から有効"
        spans = recognize_temporal_spans(source, timezone_offset="+09:00")
        self.assertEqual(len(spans), 1)
        span = spans[0]
        raw = source.encode("utf-8")
        self.assertEqual(raw[span["start_offset"]:span["end_offset"]].decode("utf-8"), span["exact_text"])
        self.assertEqual(span["offset_basis"], "UTF8_BYTE_NFC")
        self.assertIn("TEMPORAL_ROLE:VALID_FROM", span["labels"])

    def test_temporal_candidate_is_in_runtime_proposal_without_core_mutation(self):
        from datetime import datetime, timezone, timedelta
        with tempfile.TemporaryDirectory() as tmp:
            runtime = LLMSemanticRuntime(StubProvider([result()]), "ctx", "source")
            ref = datetime(2026, 1, 1, 9, 0, tzinfo=timezone(timedelta(hours=9)))
            with SemanticCoreBridge(pathlib.Path(tmp) / "temporal.sgj") as core:
                out = runtime.process("契約は2026年4月1日から有効", core,
                    reference_datetime=ref, timezone_offset="+09:00")
                spans = out["proposal"]["candidate_spans"]
                self.assertEqual(len(spans), 1)
                self.assertIn("TEMPORAL_STATUS:RESOLVED", spans[0]["labels"])
                self.assertEqual(out["temporal_candidates"][0]["valid_from"], "2026-04-01T00:00:00+09:00")
                self.assertTrue(out["semantic_commit"]["committed"])
                self.assertEqual(core.state_version, 1)

    def test_50_independent_claim_inputs_commit_and_replay(self):
        with tempfile.TemporaryDirectory() as tmp:
            outputs=[result(topic="予算",proposition=f"提案{n}",polarity="POSITIVE")
                     for n in range(50)]
            runtime=LLMSemanticRuntime(StubProvider(outputs),"ctx-50","source-50")
            journal=pathlib.Path(tmp)/"fifty.sgj"
            with SemanticCoreBridge(journal) as core:
                for n in range(50):
                    out=runtime.process(f"予算に関する提案{n}を承認します。",core)
                    self.assertTrue(out["semantic_commit"]["committed"])
                self.assertEqual(core.packet_count,50)
                self.assertEqual(core.state_version,50)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.packet_count,50)
                self.assertEqual(recovered.state_version,50)

if __name__=="__main__":
    unittest.main()
