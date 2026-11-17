import pathlib, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.compact_recognition import CompactModelSpec, CompactRecognitionRuntime, RecognitionCandidate, RecognitionPacketMetadata
from surge_adapter.recognition_candidate_scoring import RecognitionCandidateScoringBridge
from surge_adapter.compact_recognition_pipeline import CompactRecognitionPipeline

class Backend:
    def recognize(self,text,context=None):
        return [RecognitionCandidate("CLAIM","予算","予算を確保する","POSITIVE",False,.95,.08,.9,0,len(text.encode()))]
class RelationScorer:
    def score_relations(self,candidate,context):
        return {"relevance":.8,"relation_strength":.7,"novelty":.4}
class PacketAdapter:
    def __init__(self): self.calls=[]
    def admit_to_core(self,packet,source_snapshot,core):
        self.calls.append((packet,source_snapshot,core)); return {"status":"ADMITTED","packet_id":packet["packet_id"]}

class PipelineTests(unittest.TestCase):
    def make_pipeline(self,tmp):
        model=pathlib.Path(tmp)/"model.bin"; model.write_bytes(b"model")
        runtime=CompactRecognitionRuntime(CompactModelSpec.inspect("small-ja","1",str(model),runtime_id="fake-cpu"),Backend())
        bridge=RecognitionCandidateScoringBridge(RelationScorer())
        return CompactRecognitionPipeline(runtime,bridge)
    def metadata(self):
        return RecognitionPacketMetadata("p1","i1","ctx","stream","turn1",1,"g1",1,0,"compact","0.1","cap1")
    def test_prepare_runs_end_to_end_without_core_mutation(self):
        with tempfile.TemporaryDirectory() as tmp:
            pipeline=self.make_pipeline(tmp)
            result=pipeline.prepare("予算を確保する",self.metadata(),context={"focus_refs":["budget"]})
            self.assertEqual(result.accepted_count,1)
            self.assertEqual(result.review_count,0)
            self.assertEqual(result.abstained_count,0)
            self.assertFalse(result.commit_ready)
            self.assertEqual(result.packet_build.packet["packet_id"],"p1")
            self.assertEqual(result.packet_build.proposal_batch["source_digest"],result.packet_build.packet["content"]["normalized_digest"])
    def test_explicit_admission_is_separate_from_prepare(self):
        with tempfile.TemporaryDirectory() as tmp:
            pipeline=self.make_pipeline(tmp)
            result=pipeline.prepare("予算を確保する",self.metadata())
            adapter=PacketAdapter(); core=object()
            admission=pipeline.admit_packet(result,adapter,core)
            self.assertEqual(admission["status"],"ADMITTED")
            self.assertEqual(len(adapter.calls),1)
            self.assertIs(adapter.calls[0][2],core)
    def test_nfc_source_is_preserved_for_packet_admission(self):
        with tempfile.TemporaryDirectory() as tmp:
            pipeline=self.make_pipeline(tmp)
            source="か\u3099"
            result=pipeline.prepare(source,self.metadata())
            self.assertEqual(result.packet_build.packet["content"]["text"],"が")
            adapter=PacketAdapter()
            pipeline.admit_packet(result,adapter,object())
            self.assertEqual(adapter.calls[0][1],source)
    def test_abstained_candidate_is_not_accepted(self):
        class AmbiguousBackend:
            def recognize(self,text,context=None):
                return [RecognitionCandidate("CLAIM",None,"不明","UNKNOWN",False,.9,.9,.9,0,len(text.encode()))]
        with tempfile.TemporaryDirectory() as tmp:
            model=pathlib.Path(tmp)/"model.bin"; model.write_bytes(b"model")
            runtime=CompactRecognitionRuntime(CompactModelSpec.inspect("small-ja","1",str(model),runtime_id="fake"),AmbiguousBackend())
            pipeline=CompactRecognitionPipeline(runtime,RecognitionCandidateScoringBridge(RelationScorer()))
            result=pipeline.prepare("不明",self.metadata())
            self.assertEqual(result.abstained_count,1)
            self.assertEqual(result.accepted_count,0)
            self.assertFalse(result.commit_ready)

if __name__=="__main__": unittest.main()
