import unittest
from tools.summarize_model_matrix import summarize

class ModelMatrixSummaryTests(unittest.TestCase):
 def test_invalid_json_fails_execution_gate(self):
  report={"corpus_status":"SEED_NOT_INDEPENDENT","models":[{"name":"m","results":[{"json_valid":False,"timeout":False}]}]}
  result=summarize(report)
  self.assertEqual(result["model_matrix_execution_gate"],"FAIL")
  self.assertEqual(result["recognition_quality_gate"],"NOT_RUN")
 def test_timeout_and_missing_rss_fail(self):
  report={"corpus_status":"SEED_NOT_INDEPENDENT","models":[{"name":"m","results":[{"json_valid":True,"timeout":True}]}]}
  self.assertEqual(summarize(report)["model_matrix_execution_gate"],"FAIL")
if __name__=="__main__": unittest.main()
