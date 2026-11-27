import unittest
import json
from pathlib import Path
from tools.evaluate_gold_corpus import evaluate_case, evaluate
from tools.validate_projector_output import validate as validate_projector_output


class GoldCorpusEvaluatorTests(unittest.TestCase):
    def test_attribute_relation_and_evidence_exact_match(self):
        case = {"case_id": "x", "text": "締切は水曜日", "gold": {"schema_version": "surge-projector-output-v1.0", 
            "attributes": [{"key": "deadline", "value": "水曜日", "polarity": "positive", "evidence_spans": [{"source": "text", "start": 3, "end": 6, "quote": "水曜日"}]}],
            "relations": [{"type": "supersedes", "source": "new", "target": "old", "evidence_spans": [{"source": "text", "start": 0, "end": 7, "quote": "締切は水曜日"}]}]}}
        pred = {"attributes": [{"key": "deadline", "value": "水曜日",
                                "polarity": "positive", "evidence_spans": [{"source": "text", "start": 3, "end": 6, "quote": "水曜日"}]}],
                "relations": [{"type": "supersedes", "source": "new", "target": "old", "evidence_spans": [{"source": "text", "start": 0, "end": 7, "quote": "締切は水曜日"}]}]}
        result = evaluate_case(case, pred)
        self.assertEqual(result["attribute_metrics"]["f1"], 1.0)
        self.assertEqual(result["relation_metrics"]["f1"], 1.0)
        self.assertEqual(result["evidence_support_rate"], 1.0)

    def test_empty_gold_and_empty_prediction_never_score_perfect(self):
        result = evaluate_case({"case_id": "empty", "text": "x",
                                "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": []}},
                               {"attributes": [], "relations": []})
        self.assertEqual(result["attribute_metrics"]["precision"], 0.0)
        self.assertEqual(result["attribute_metrics"]["recall"], 0.0)

    def test_extra_attribute_and_relation_are_false_positives(self):
        case = {"case_id": "x", "text": "A", "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": []}}
        result = evaluate_case(case, {"attributes": [{"key": "x", "value": "invented"}],
                                      "relations": [{"type": "supports", "source": "x", "target": "y"}]})
        self.assertEqual(result["attribute_metrics"]["fp"], 1)
        self.assertEqual(result["relation_metrics"]["fp"], 1)

    def test_duplicate_outputs_count_as_false_positives(self):
        case = {"case_id": "dup", "text": "期限は水曜", "gold": {"schema_version": "surge-projector-output-v1.0", 
            "attributes": [{"key": "期限", "value": "水曜"}], "relations": []}}
        pred = {"attributes": [
            {"key": "期限", "value": "水曜", "evidence_quote": "期限は水曜"},
            {"key": "期限", "value": "水曜", "evidence_quote": "期限は水曜"}], "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["duplicate_attribute_count"], 1)
        self.assertEqual(result["attribute_metrics"]["fp"], 1)

    def test_wrong_category_is_fp_and_gold_category_is_fn(self):
        case = {"case_id": "category", "categories": ["negation"], "text": "参加しない",
                "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [{"key": "参加", "value": "参加",
                                         "polarity": "negative", "category": "negation"}],
                         "relations": []}}
        pred = {"attributes": [{"key": "参加", "value": "参加", "polarity": "negative",
                                "category": "affirmation", "evidence_quote": "参加しない"}],
                "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["categories"]["negation"]["fn"], 1)
        self.assertEqual(result["categories"]["affirmation"]["fp"], 1)

    def test_missing_category_is_not_silently_dropped(self):
        case = {"case_id": "category", "text": "参加する",
                "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [{"key": "参加", "value": "参加", "category": "affirmation"}],
                         "relations": []}}
        result = evaluate_case(case, {"attributes": [{"key": "参加", "value": "参加"}],
                                      "relations": []})
        self.assertEqual(result["categories"]["affirmation"]["fn"], 1)
        self.assertEqual(result["categories"]["__missing_category__"]["fp"], 1)

    def test_missing_evidence_is_unsupported(self):
        case = {"case_id": "ev", "text": "期限は水曜",
                "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [{"key": "期限", "value": "水曜"}], "relations": []}}
        result = evaluate_case(case, {"attributes": [{"key": "期限", "value": "水曜"}],
                                      "relations": []})
        self.assertEqual(result["unsupported_attribute_count"], 1)


    def test_valid_but_unrelated_span_does_not_support_attribute(self):
        case = {
            "case_id": "unrelated-evidence",
            "text": "期限は水曜です。担当は佐藤です。",
            "gold": {
                "schema_version": "surge-projector-output-v1.0",
                "attributes": [{
                    "key": "期限", "value": "水曜", "polarity": "positive",
                    "category": "temporal_update",
                    "evidence_spans": [{
                        "source": "text", "start": 3, "end": 5, "quote": "水曜"
                    }]
                }],
                "relations": []
            }
        }
        prediction = {
            "schema_version": "surge-projector-output-v1.0",
            "attributes": [{
                "key": "期限", "value": "水曜", "polarity": "positive",
                "category": "temporal_update",
                "evidence_spans": [{
                    "source": "text", "start": 10, "end": 12, "quote": "佐藤"
                }]
            }],
            "relations": []
        }
        result = evaluate_case(case, prediction)
        self.assertEqual(result["unsupported_attribute_count"], 1)
        self.assertEqual(result["evidence_support_rate"], 0.0)

    def test_unadjudicated_seed_is_not_run(self):
        corpus = {"schema_version": "surge-gold-corpus-v1.0", "cases": [{"case_id": "seed", "split": "seed", "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": []}}],
                  "annotation": {"independent_adjudication": False}, "profile": {}}
        self.assertEqual(evaluate(corpus, {}, "digest")["recognition_quality_gate"], "NOT_RUN")

    def test_missing_test_prediction_fails_closed(self):
        corpus = {"schema_version": "surge-gold-corpus-v1.0", "cases": [{"case_id": "test", "split": "test", "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": []}}],
                  "annotation": {"independent_adjudication": True}, "profile": {}}
        self.assertEqual(evaluate(corpus, {}, "digest")["recognition_quality_gate"], "FAIL")

    def test_extra_prediction_case_fails_closed(self):
        corpus = {"schema_version": "surge-gold-corpus-v1.0", "cases": [{"case_id": "test", "split": "test", "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": []}}],
                  "annotation": {"independent_adjudication": True}, "profile": {}}
        result = evaluate(corpus, {"test": {"attributes": [], "relations": []},
                                   "unexpected": {"attributes": [], "relations": []}}, "digest")
        self.assertEqual(result["recognition_quality_gate"], "FAIL")
        self.assertEqual(result["extra_case_ids"], ["unexpected"])


    def test_legacy_candidates_alias_is_not_canonical_schema(self):
        case = {"case_id": "schema", "text": "x",
                "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": []}}
        result = evaluate_case(case, {"candidates": [], "relations": []})
        self.assertFalse(result["schema_valid"])
        self.assertEqual(result["malformed_count"], 1)


    def test_exact_evidence_span_precision_recall(self):
        span = {"source": "text", "start": 3, "end": 6, "quote": "水曜日"}
        case = {"case_id": "span", "text": "締切は水曜日",
                "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [{"key": "deadline", "value": "水曜日",
                                         "polarity": "positive", "category": "temporal",
                                         "evidence_spans": [span]}], "relations": []}}
        pred = {"attributes": [{"key": "deadline", "value": "水曜日",
                                "polarity": "positive", "category": "temporal",
                                "evidence_spans": [span]}], "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["evidence_span_metrics"]["precision"], 1.0)
        self.assertEqual(result["evidence_span_metrics"]["recall"], 1.0)

    def test_invalid_evidence_span_is_malformed_and_unsupported(self):
        case = {"case_id": "span-invalid", "text": "期限は水曜",
                "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [{"key": "期限", "value": "水曜"}], "relations": []}}
        pred = {"attributes": [{"key": "期限", "value": "水曜",
                                "evidence_spans": [{"source": "text", "start": 0,
                                                    "end": 2, "quote": "誤った引用"}]}],
                "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["malformed_count"], 1)
        self.assertEqual(result["unsupported_attribute_count"], 1)

    def test_span_evaluation_requires_gold_spans_and_threshold(self):
        corpus = {"schema_version": "surge-gold-corpus-v1.0", "evidence_span_evaluation": True,
                  "cases": [{"case_id": "span-profile", "split": "test", "text": "期限は水曜", "phenomena": [],
                             "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [{"key": "期限", "value": "水曜"}],
                                      "relations": []}}],
                  "annotation": {"independent_adjudication": True},
                  "profile": {"attribute_f1_min": 0, "relation_f1_min": 0,
                              "macro_f1_min": 0, "evidence_support_min": 0,
                              "schema_valid_rate_min": 0,
                              "false_positive_relation_rate_max": 1,
                              "category_recall_min": {"temporal": 0}}}
        pred = {"span-profile": {"attributes": [{"key": "期限", "value": "水曜"}],
                                 "relations": []}}
        result = evaluate(corpus, pred, "corpus")
        self.assertEqual(result["recognition_quality_gate"], "FAIL")
        self.assertIn("Evidence Spans", result["reason"])


    def test_evaluator_schema_rate_uses_canonical_schema(self):
        case = {"case_id": "canonical", "text": "期限は水曜",
                "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [{"key": "期限", "value": "水曜"}],
                         "relations": []}}
        pred = {"schema_version": "surge-projector-output-v1.0",
                "attributes": [{"key": "期限", "value": "水曜",
                                "polarity": "positive", "category": "temporal",
                                "evidence_spans": [{"source": "text", "start": 3,
                                                    "end": 5, "quote": "水曜"}]}],
                "relations": []}
        result = evaluate_case(case, pred)
        self.assertTrue(result["schema_valid"])
        self.assertEqual(result["canonical_schema_error_count"], 0)


    def test_independent_phenomenon_metrics_are_separate(self):
        cases = [
            {"case_id":"n","split":"test","text":"参加しません","categories":["negation"],
             "phenomena":["negation"],"gold":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"参加","value":"参加",
             "polarity":"negative","category":"negation","evidence_spans":[{"source":"text","start":2,"end":6,"quote":"しません"}]}],"relations":[]}},
            {"case_id":"c","split":"test","context":"会議は火曜","text":"会議は水曜に変更",
             "categories":["contradiction"],"phenomena":["contradiction"],
             "gold":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"会議日","value":"水曜","polarity":"positive","category":"contradiction",
             "evidence_spans":[{"source":"text","start":3,"end":5,"quote":"水曜"}]}],
             "relations":[{"type":"contradicts","source":"水曜","target":"火曜","evidence_spans":[{"source":"text","start":3,"end":5,"quote":"水曜"}]}]}},
            {"case_id":"t","split":"test","context":"期限は3月","text":"4月に更新",
             "categories":["temporal_update"],"phenomena":["temporal_supersession"],
             "gold":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"期限","value":"4月","polarity":"positive","category":"temporal_update",
             "evidence_spans":[{"source":"text","start":0,"end":2,"quote":"4月"}]}],
             "relations":[{"type":"supersedes","source":"4月","target":"3月","evidence_spans":[{"source":"text","start":0,"end":2,"quote":"4月"}]}]}},
            {"case_id":"a","split":"test","text":"それで進めましょう","categories":["ambiguity"],
             "phenomena":["ambiguity"],"gold":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"対象","value":"未確定","polarity":"uncertain","category":"ambiguity",
             "evidence_spans":[{"source":"text","start":0,"end":9,"quote":"それで進めましょう"}]}],"relations":[]}}
        ]
        predictions = {
            "n":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"参加","value":"参加","polarity":"negative","category":"negation","evidence_spans":[{"source":"text","start":2,"end":6,"quote":"しません"}]}],"relations":[],"phenomena":[{"label":"negation","evidence_spans":[{"source":"text","start":2,"end":6,"quote":"しません"}]}]},
            "c":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"会議日","value":"水曜","polarity":"positive","category":"contradiction","evidence_spans":[{"source":"text","start":3,"end":5,"quote":"水曜"}]}],"relations":[{"type":"contradicts","source":"水曜","target":"火曜","evidence_spans":[{"source":"text","start":3,"end":5,"quote":"水曜"}]}],"phenomena":[{"label":"contradiction","evidence_spans":[{"source":"text","start":3,"end":5,"quote":"水曜"}]}]},
            "t":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"期限","value":"4月","polarity":"positive","category":"temporal_update","evidence_spans":[{"source":"text","start":0,"end":2,"quote":"4月"}]}],"relations":[{"type":"supersedes","source":"4月","target":"3月","evidence_spans":[{"source":"text","start":0,"end":2,"quote":"4月"}]}],"phenomena":[{"label":"temporal_supersession","evidence_spans":[{"source":"text","start":0,"end":2,"quote":"4月"}]}]},
            "a":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"対象","value":"未確定","polarity":"uncertain","category":"ambiguity","evidence_spans":[{"source":"text","start":0,"end":9,"quote":"それで進めましょう"}]}],"relations":[],"phenomena":[{"label":"ambiguity","evidence_spans":[{"source":"text","start":0,"end":9,"quote":"それで進めましょう"}]}]}
        }
        profile={"attribute_f1_min":0,"relation_f1_min":0,"macro_f1_min":0,
                 "evidence_support_min":0,"evidence_span_f1_min":0,"schema_valid_rate_min":1,
                 "false_positive_relation_rate_max":1,
                 "category_precision_min":{x:0 for x in ("negation","contradiction","temporal_update","ambiguity")},
                 "category_recall_min":{x:0 for x in ("negation","contradiction","temporal_update","ambiguity")},
                 "phenomenon_precision_min":{x:1 for x in ("negation","contradiction","temporal_supersession","ambiguity")},
                 "phenomenon_recall_min":{x:1 for x in ("negation","contradiction","temporal_supersession","ambiguity")}}
        result=evaluate({"schema_version":"surge-gold-corpus-v1.0","cases":cases,"annotation":{"independent_adjudication":True},
                         "profile":profile,"evidence_span_evaluation":True},predictions,"digest")
        self.assertEqual(result["recognition_quality_gate"],"PASS",result.get("failures"))
        for name in ("negation","contradiction","temporal_supersession","ambiguity"):
            self.assertEqual(result["phenomenon_metrics"][name]["f1"],1.0)

    def test_phenomena_are_not_inferred_from_attribute_polarity(self):
        case = {"case_id": "no-inference", "text": "参加しません",
                "phenomena": [], "gold": {"schema_version": "surge-projector-output-v1.0",
                "attributes": [{"key": "参加", "value": "参加", "polarity": "negative",
                                "category": "negation",
                                "evidence_spans": [{"source": "text", "start": 2, "end": 6, "quote": "しません"}]}],
                "relations": []}}
        pred = {"schema_version": "surge-projector-output-v1.0",
                "attributes": [{"key": "参加", "value": "参加", "polarity": "negative",
                                "category": "negation",
                                "evidence_spans": [{"source": "text", "start": 2, "end": 6, "quote": "しません"}]}],
                "relations": []}
        result = evaluate_case(case, pred)
        self.assertEqual(result["predicted_phenomena"], [])
        self.assertEqual(result["phenomenon_metrics"]["negation"]["fp"], 0)

    def test_phenomenon_requires_valid_explicit_evidence(self):
        case = {"case_id": "phenomenon-evidence", "text": "参加しません",
                "phenomena": ["negation"],
                "gold": {"schema_version": "surge-projector-output-v1.0",
                         "attributes": [], "relations": []}}
        pred = {"schema_version": "surge-projector-output-v1.0", "attributes": [],
                "relations": [], "phenomena": [{"label": "negation",
                "evidence_spans": [{"source": "text", "start": 0, "end": 2, "quote": "違う"}]}]}
        result = evaluate_case(case, pred)
        self.assertEqual(result["predicted_phenomena"], [])
        self.assertGreater(result["malformed_count"], 0)

    def test_category_precision_floor_is_enforced(self):
        case={"case_id":"p","split":"test","text":"参加する","categories":["affirmation"],"phenomena":[],
              "gold":{"schema_version":"surge-projector-output-v1.0","attributes":[{"key":"参加","value":"参加","polarity":"positive","category":"affirmation",
              "evidence_spans":[{"source":"text","start":0,"end":4,"quote":"参加する"}]}],"relations":[]}}
        pred={"p":{"schema_version":"surge-projector-output-v1.0","attributes":[
              {"key":"参加","value":"参加","polarity":"positive","category":"wrong",
               "evidence_spans":[{"source":"text","start":0,"end":4,"quote":"参加する"}]}],"relations":[]}}
        floors={"affirmation":0.9}
        profile={"attribute_f1_min":0,"relation_f1_min":0,"macro_f1_min":0,
                 "evidence_support_min":0,"evidence_span_f1_min":0,"schema_valid_rate_min":1,
                 "false_positive_relation_rate_max":1,"category_precision_min":floors,
                 "category_recall_min":{"affirmation":0},
                 "phenomenon_precision_min":{x:0 for x in ("negation","contradiction","temporal_supersession","ambiguity")},
                 "phenomenon_recall_min":{x:0 for x in ("negation","contradiction","temporal_supersession","ambiguity")}}
        result=evaluate({"schema_version":"surge-gold-corpus-v1.0","cases":[case],"annotation":{"independent_adjudication":True},
                         "profile":profile,"evidence_span_evaluation":True},pred,"digest")
        self.assertIn("category_precision:affirmation",result["failures"])


    def test_seed_v1_gold_spans_and_category_threshold_coverage(self):
        corpus=json.loads((Path(__file__).parent/"fixtures/semantic/gold_seed_v0.1.json").read_text(encoding="utf-8"))
        self.assertEqual(corpus["schema_version"],"surge-gold-corpus-v1.0")
        expected={a["category"] for case in corpus["cases"] for a in case["gold"]["attributes"]}
        self.assertEqual(set(corpus["profile"]["category_precision_min"]),expected)
        self.assertEqual(set(corpus["profile"]["category_recall_min"]),expected)
        self.assertFalse(corpus["annotation"]["independent_adjudication"])
        for case in corpus["cases"]:
            self.assertEqual(validate_projector_output(case["gold"]),[])
            sources={"context":case.get("context",""),"text":case.get("text","")}
            for item in case["gold"]["attributes"]+case["gold"]["relations"]:
                self.assertTrue(item["evidence_spans"],case["case_id"])
                for span in item["evidence_spans"]:
                    self.assertEqual(sources[span["source"]][span["start"]:span["end"]],span["quote"])

    def test_test_cases_require_explicit_phenomenon_annotations(self):
        case={"case_id":"no-phenomena","split":"test","text":"x",
              "gold":{"schema_version":"surge-projector-output-v1.0","attributes":[],"relations":[]}}
        result=evaluate({"schema_version":"surge-gold-corpus-v1.0","cases":[case],"annotation":{"independent_adjudication":True},
                         "profile":{}},{"no-phenomena":{"schema_version":"surge-projector-output-v1.0",
                         "attributes":[],"relations":[]}},"digest")
        self.assertEqual(result["recognition_quality_gate"],"FAIL")
        self.assertIn("phenomena",result["reason"])


    def test_invalid_gold_span_returns_structured_fail(self):
        case={"case_id":"bad-span","split":"test","text":"期限は水曜","phenomena":[],
              "gold":{"schema_version":"surge-projector-output-v1.0",
                      "attributes":[{"key":"期限","value":"水曜","polarity":"positive",
                                     "category":"temporal","evidence_spans":[
                                         {"source":"text","start":0,"end":2,"quote":"違う"}]}],
                      "relations":[]}}
        pred={"bad-span":{"schema_version":"surge-projector-output-v1.0","attributes":[],"relations":[]}}
        result=evaluate({"schema_version":"surge-gold-corpus-v1.0","cases":[case],
                         "annotation":{"independent_adjudication":True}},pred,"digest")
        self.assertEqual(result["recognition_quality_gate"],"FAIL")
        self.assertIn("invalid Gold",result["reason"])

    def test_duplicate_corpus_case_id_rejected(self):
        case = {"case_id": "same", "split": "test", "gold": {"schema_version": "surge-projector-output-v1.0", "attributes": [], "relations": []}}
        with self.assertRaises(ValueError):
            evaluate({"schema_version":"surge-gold-corpus-v1.0","cases": [case, case], "annotation": {"independent_adjudication": True}},
                     {"same": {"attributes": [], "relations": []}}, "digest")


if __name__ == "__main__":
    unittest.main()
