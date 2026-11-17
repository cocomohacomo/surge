import pathlib, sys, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.compact_recognition import RecognitionCandidate
from surge_adapter.recognition_candidate_scoring import RecognitionCandidateScoringBridge, partition_assessments

class RelationScorer:
    def __init__(self,scores=None): self.scores=scores or {"relevance":.8,"relation_strength":.7,"novelty":.4}
    def score_relations(self,candidate,context): return self.scores

def batch(confidence=.95,ambiguity=.08,importance=.9):
    c=RecognitionCandidate("CLAIM","予算","予算を確保する","POSITIVE",False,confidence,ambiguity,importance,0,len("予算を確保する".encode()))
    return {"schema_version":"0.1.0","packet_id":"p-1","source_digest":"a"*64,"model_id":"small-ja","model_bytes":1024,
    "candidates":[{"kind":c.kind,"topic":c.topic,"proposition":c.proposition,"polarity":c.polarity,
    "conditional":c.conditional,"confidence":c.confidence,"ambiguity":c.ambiguity,"importance":c.importance,
    "start_offset":c.start_offset,"end_offset":c.end_offset}]}

class Tests(unittest.TestCase):
    def setUp(self): self.bridge=RecognitionCandidateScoringBridge(RelationScorer())
    def assess(self,b=None): return self.bridge.assess_batch(b or batch(),expected_packet_id="p-1",expected_source_digest="a"*64,context={"focus_refs":["cluster:budget"]})
    def test_accept_is_proposal_only(self):
        x=self.assess()[0]; self.assertEqual(x.decision,"ACCEPT"); self.assertFalse(x.commit_ready)
    def test_ambiguity_abstains(self): self.assertEqual(self.assess(batch(ambiguity=.8))[0].decision,"ABSTAIN")
    def test_medium_confidence_review(self): self.assertEqual(self.assess(batch(confidence=.8))[0].decision,"REVIEW")
    def test_digest_binding(self):
        b=batch(); b["source_digest"]="b"*64
        with self.assertRaisesRegex(ValueError,"PROPOSAL_SOURCE_DIGEST_MISMATCH"): self.assess(b)
    def test_packet_binding(self):
        b=batch(); b["packet_id"]="other"
        with self.assertRaisesRegex(ValueError,"PROPOSAL_PACKET_ID_MISMATCH"): self.assess(b)
    def test_missing_relation_dimensions_rejected(self):
        bridge=RecognitionCandidateScoringBridge(RelationScorer({"relevance":.5}))
        with self.assertRaisesRegex(ValueError,"RELATION_SCORE_FIELDS_INVALID"):
            bridge.assess_batch(batch(),expected_packet_id="p-1",expected_source_digest="a"*64)
    def test_nonfinite_score_rejected(self):
        bridge=RecognitionCandidateScoringBridge(RelationScorer({"relevance":float("nan"),"relation_strength":.5,"novelty":.5}))
        with self.assertRaises(ValueError): bridge.assess_batch(batch(),expected_packet_id="p-1",expected_source_digest="a"*64)
    def test_identity_deterministic(self): self.assertEqual(self.assess()[0].candidate_id,self.assess()[0].candidate_id)
    def test_partition_never_commit_ready(self):
        out=partition_assessments(self.assess()+self.assess(batch(ambiguity=.8)))
        self.assertEqual(len(out["accepted"]),1); self.assertEqual(len(out["abstained"]),1); self.assertFalse(out["commit_ready"])

if __name__=="__main__": unittest.main()
