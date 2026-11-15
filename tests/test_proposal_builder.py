import pathlib, sys, unittest
from jsonschema import Draft202012Validator
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.recognition_scoring import RecognitionScorer, SCORE_FIELDS
from surge_adapter.proposal_builder import build_classification_proposal, SCHEMA_PATH

def packet():
 return {"packet_id":"p-1","idempotency_key":"idem-1",
  "source_position":{"start_offset":0,"end_offset":10,"offset_basis":"UTF8_BYTE_NFC",
   "source_generation":"g-1","source_sequence":7},
  "context_hint":{"state_version":4}}

class ProposalBuilderTests(unittest.TestCase):
 def setUp(self):
  self.scorer=RecognitionScorer()
  self.schema=Draft202012Validator(__import__("json").loads(SCHEMA_PATH.read_text(encoding="utf-8")))
  self.base={"proposal_id":"prop-1","producer_id":"scorer","producer_version":"0.1",
   "model_descriptor":"manual-reference","runtime_descriptor":"python-test",
   "base_state_version":4,"candidate_labels":[{"family":"SPEECH_FUNCTION","label":"DECISION_ACCEPTANCE","score_dimension":"importance"}],
   "candidate_spans":[{"start_offset":1,"end_offset":5,"offset_basis":"UTF8_BYTE_NFC","labels":["DECISION"],"candidate_kind":"CLAIM_CUE"}]}
 def test_builds_schema_valid_non_committing_proposal(self):
  scores={k:.9 for k in SCORE_FIELDS}; scores["ambiguity"]=.1
  scoring=self.scorer.evaluate("p-1",scores)
  result=build_classification_proposal(packet(),scoring,**self.base)
  self.assertEqual(list(self.schema.iter_errors(result)),[])
  self.assertEqual(result["packet_ref"],"p-1")
  self.assertEqual(result["labels"][0]["score"],.9)
  self.assertEqual(result["abstention_reasons"],[])
  self.assertNotIn("state_delta",result)
 def test_abstention_is_carried_into_proposal(self):
  scores={k:.9 for k in SCORE_FIELDS}; scores["confidence"]=.4
  scoring=self.scorer.evaluate("p-1",scores)
  result=build_classification_proposal(packet(),scoring,**self.base)
  self.assertIn("LOW_MARGIN",result["abstention_reasons"])
  self.assertIn("SCORING_DETAIL:LOW_CONFIDENCE",result["warnings"])
 def test_out_of_packet_span_rejected(self):
  scoring=self.scorer.evaluate("p-1",{k:.9 for k in SCORE_FIELDS})
  args=dict(self.base); args["candidate_spans"]=[{"start_offset":2,"end_offset":12,"offset_basis":"UTF8_BYTE_NFC","labels":["X"]}]
  with self.assertRaisesRegex(ValueError,"CANDIDATE_SPAN_OUT_OF_PACKET_RANGE"):
   build_classification_proposal(packet(),scoring,**args)
 def test_unknown_score_dimension_rejected(self):
  scoring=self.scorer.evaluate("p-1",{k:.9 for k in SCORE_FIELDS})
  args=dict(self.base); args["candidate_labels"]=[{"family":"SPEECH_FUNCTION","label":"X","score_dimension":"magic"}]
  with self.assertRaisesRegex(ValueError,"UNKNOWN_SCORE_DIMENSION"):
   build_classification_proposal(packet(),scoring,**args)
if __name__=="__main__": unittest.main()
