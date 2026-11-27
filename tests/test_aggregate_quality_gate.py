import unittest
from tools.aggregate_quality_gate import gate

class AggregateGateTests(unittest.TestCase):
 def test_both_tracks_same_identity_pass(self):
  identity={"commit_sha":"abc","profile_sha256":"profile","corpus_sha256":"corpus"}
  core={"core_integrity_gate":"PASS",**identity}
  rec={"recognition_quality_gate":"PASS",**identity}
  self.assertEqual(gate(core,rec,identity)["aggregate_gate"],"PASS")
 def test_not_run_fails(self):
  identity={"commit_sha":"abc","profile_sha256":"profile","corpus_sha256":"corpus"}
  core={"core_integrity_gate":"PASS",**identity}
  rec={"recognition_quality_gate":"NOT_RUN",**identity}
  self.assertEqual(gate(core,rec,identity)["aggregate_gate"],"FAIL")
 def test_identity_mismatch_fails(self):
  expected={"commit_sha":"abc","profile_sha256":"profile","corpus_sha256":"corpus"}
  core={"core_integrity_gate":"PASS",**expected}
  rec={"recognition_quality_gate":"PASS","commit_sha":"different","profile_sha256":"profile","corpus_sha256":"corpus"}
  self.assertEqual(gate(core,rec,expected)["aggregate_gate"],"FAIL")
 def test_missing_evidence_fails(self):
  self.assertEqual(gate({"core_integrity_gate":"PASS"},{"recognition_quality_gate":"PASS"}, {})["aggregate_gate"],"FAIL")
if __name__=="__main__": unittest.main()
