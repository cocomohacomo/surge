import unittest
from tools.aggregate_quality_gate import gate
class AggregateGateTests(unittest.TestCase):
 def test_both_tracks_pass_required(self):
  self.assertEqual(gate({"gate_status":"PASS"},{"recognition_quality_gate":"PASS"})["aggregate_gate"],"PASS")
 def test_fail_or_not_run_blocks(self):
  for status in ("FAIL","NOT_RUN","MISSING"):
   result=gate({"gate_status":"PASS"},{"recognition_quality_gate":status})
   self.assertEqual(result["aggregate_gate"],"FAIL")
   self.assertIn("recognition_quality",result["blocked_tracks"])
if __name__=="__main__": unittest.main()
