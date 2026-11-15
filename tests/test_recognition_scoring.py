import json, pathlib, sys, unittest
from jsonschema import Draft202012Validator
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.recognition_scoring import RecognitionScorer, SCORE_FIELDS
CORPUS=ROOT/"tests/conformance/SURGE_JA_SCORING_50_v0.1.json"
SCHEMA=ROOT/"schemas/surge-v0.9-scoring.schema.json"
class RecognitionScoringTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.corpus=json.loads(CORPUS.read_text(encoding="utf-8"))
  schema=json.loads(SCHEMA.read_text(encoding="utf-8")); Draft202012Validator.check_schema(schema)
  cls.validator=Draft202012Validator(schema); cls.scorer=RecognitionScorer()
 def test_all_50_cases_schema_and_expected_decision(self):
  self.assertEqual(len(self.corpus["cases"]),50); ids=set()
  for case in self.corpus["cases"]:
   self.assertNotIn(case["case_id"],ids); ids.add(case["case_id"])
   result=self.scorer.evaluate(case["case_id"],case["scores"]).as_dict()
   self.assertEqual(list(self.validator.iter_errors(result)),[])
   self.assertEqual(result["priority_score"],case["scores"]["importance"])
   self.assertEqual(result["decision"],case["expected_decision"])
 def test_novelty_is_an_independent_dimension(self):
  self.assertTrue(any(c["scores"]["novelty"] != c["scores"]["importance"] for c in self.corpus["cases"]))
 def test_importance_is_not_confidence(self):
  scores={k:.9 for k in SCORE_FIELDS}; scores["importance"]=.98; scores["confidence"]=.4
  result=self.scorer.evaluate("separate-axes",scores)
  self.assertEqual(result.priority_score,.98); self.assertEqual(result.decision,"ABSTAIN")
  self.assertIn("LOW_CONFIDENCE",result.abstention_reasons)
 def test_ambiguous_reference_abstains(self):
  scores={k:.9 for k in SCORE_FIELDS}; scores["importance"]=.9; scores["ambiguity"]=.8
  result=self.scorer.evaluate("ambiguous",scores)
  self.assertEqual(result.priority_score,.9); self.assertEqual(result.decision,"ABSTAIN")
  self.assertIn("AMBIGUOUS_REFERENCE",result.abstention_reasons)
 def test_invalid_scores_rejected(self):
  scores={k:.5 for k in SCORE_FIELDS}; scores["importance"]=1.1
  with self.assertRaisesRegex(ValueError,"SCORE_OUT_OF_RANGE"): self.scorer.evaluate("bad",scores)
  scores["importance"]=True
  with self.assertRaisesRegex(ValueError,"SCORE_NOT_NUMERIC"): self.scorer.evaluate("bad",scores)
 def test_scoring_has_no_commit_api(self):
  result=self.scorer.evaluate("review-only",{k:.9 for k in SCORE_FIELDS})
  self.assertFalse(hasattr(result,"commit")); self.assertFalse(hasattr(result,"state_delta"))
if __name__=="__main__": unittest.main()
