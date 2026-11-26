import unittest
from tools.evaluate_gold_corpus import evaluate_case, evaluate

class GoldCorpusEvaluatorTests(unittest.TestCase):
    def test_attribute_and_relation_exact_match(self):
        case={"case_id":"x","text":"締切は水曜日","gold":{"attributes":[{"key":"deadline","value":"水曜日","polarity":"positive"}],"relations":[{"type":"supersedes","source":"new","target":"old"}]}}
        prediction={"attributes":[{"key":"deadline","value":"水曜日","polarity":"positive","evidence_quote":"締切は水曜日"}],"relations":[{"type":"supersedes","source":"new","target":"old"}]}
        result=evaluate_case(case,prediction)
        self.assertEqual(result["attribute_metrics"]["f1"],1.0)
        self.assertEqual(result["relation_metrics"]["f1"],1.0)
        self.assertEqual(result["evidence_support_rate"],1.0)
    def test_extra_attributes_and_relations_are_false_positives(self):
        case={"case_id":"x","text":"A","gold":{"attributes":[],"relations":[]}}
        result=evaluate_case(case,{"attributes":[{"key":"x","value":"hallucinated"}],"relations":[{"type":"supports","source":"x","target":"y"}]})
        self.assertEqual(result["attribute_metrics"]["fp"],1)
        self.assertEqual(result["relation_metrics"]["fp"],1)
    def test_unadjudicated_seed_is_not_run(self):
        corpus={"cases":[{"case_id":"seed","split":"seed","gold":{"attributes":[],"relations":[]}}],"annotation":{"independent_adjudication":False},"profile":{}}
        result=evaluate(corpus,{}, "digest")
        self.assertEqual(result["recognition_quality_gate"],"NOT_RUN")
    def test_missing_test_prediction_fails_closed(self):
        corpus={"cases":[{"case_id":"test","split":"test","gold":{"attributes":[],"relations":[]}}],"annotation":{"independent_adjudication":True},"profile":{}}
        result=evaluate(corpus,{}, "digest")
        self.assertEqual(result["recognition_quality_gate"],"FAIL")
if __name__=="__main__": unittest.main()
