import pathlib, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.llm_interface import LLMSemanticRuntime, LLMRecognizer
from surge_adapter.semantic_core_bridge import SemanticCoreBridge

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
    def test_claim_commits_packet_evidence_claim_cluster_and_replays(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal=pathlib.Path(tmp)/"runtime.sgj"
            runtime=LLMSemanticRuntime(StubProvider([result()]),"ctx","source")
            with SemanticCoreBridge(journal) as core:
                out=runtime.process("予算を増額します。",core)
                self.assertTrue(out["semantic_commit"]["committed"])
                self.assertEqual(core.state_version,2)
                self.assertEqual(core.packet_count,1)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version,2)
                self.assertEqual(recovered.packet_count,1)

    def test_contradiction_creates_question_aspect_interest_and_relation(self):
        with tempfile.TemporaryDirectory() as tmp:
            runtime=LLMSemanticRuntime(StubProvider([
                result(), result(proposition="予算を増額",polarity="NEGATIVE")]),"ctx","source")
            with SemanticCoreBridge(pathlib.Path(tmp)/"runtime.sgj") as core:
                runtime.process("予算を増額します。",core)
                out=runtime.process("予算を増額しません。",core)
                self.assertTrue(out["semantic_commit"]["committed"])
                self.assertEqual(core.state_version,4)
                self.assertEqual(core.packet_count,2)

    def test_abstention_preserves_evidence_without_claim(self):
        with tempfile.TemporaryDirectory() as tmp:
            runtime=LLMSemanticRuntime(StubProvider([
                result(confidence=.40,ambiguity=.90)]),"ctx","source")
            with SemanticCoreBridge(pathlib.Path(tmp)/"runtime.sgj") as core:
                out=runtime.process("それは不明です。",core)
                self.assertEqual(out["decision"],"ABSTAIN")
                self.assertFalse(out["claim_created"])
                self.assertEqual(core.state_version,2)

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
                self.assertEqual(core.state_version,100)
            with SemanticCoreBridge(journal) as recovered:
                self.assertEqual(recovered.packet_count,50)
                self.assertEqual(recovered.state_version,100)

if __name__=="__main__":
    unittest.main()
