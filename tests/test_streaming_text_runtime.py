import pathlib, sys, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.text_runtime import StreamingContextRuntime, JapaneseRuleRecognizer


class StreamingTextRuntimeTests(unittest.TestCase):
    def test_text_forms_topic_cluster_and_claim(self):
        runtime=StreamingContextRuntime()
        result=runtime.process("予算を増額します。")
        state=runtime.snapshot()
        self.assertEqual(result["cluster_count"],1)
        self.assertEqual(result["claim_count"],1)
        self.assertEqual(state["claims"][0]["polarity"],"POSITIVE")
        self.assertEqual(state["active_cluster"],"cluster:予算")

    def test_contradiction_retains_both_claims_and_routes_question_interest(self):
        runtime=StreamingContextRuntime()
        runtime.process("予算を増額します。")
        result=runtime.process("予算を増額しません。")
        state=runtime.snapshot()
        self.assertEqual(len(state["claims"]),2)
        self.assertEqual(len([r for r in state["relations"] if r["type"]=="CONTRADICTS"]),1)
        self.assertEqual(result["question_count"],1)
        self.assertEqual(result["interest_count"],1)

    def test_short_acknowledgement_attaches_to_active_cluster(self):
        runtime=StreamingContextRuntime()
        runtime.process("予算を増額します。")
        result=runtime.process("うん")
        event=result["events"][0]
        self.assertEqual(event["kind"],"ACKNOWLEDGEMENT")
        self.assertEqual(event["attached_to"],"cluster:予算")

    def test_ambiguous_reference_does_not_switch_focus(self):
        runtime=StreamingContextRuntime()
        runtime.process("予算を増額します。")
        result=runtime.process("それは難しいですね。")
        self.assertEqual(result["active_cluster"],"cluster:予算")
        self.assertEqual(result["events"][0]["resolution"],"UNRESOLVED")

    def test_topic_switch_creates_parallel_cluster(self):
        runtime=StreamingContextRuntime()
        runtime.process("予算を増額します。")
        result=runtime.process("事業を開始します。")
        self.assertEqual(result["cluster_count"],2)
        self.assertEqual(result["active_cluster"],"cluster:事業")
        self.assertIn("cluster:予算",runtime.snapshot()["clusters"][0]["id"])

    def test_question_routes_interest_without_forcing_action(self):
        runtime=StreamingContextRuntime()
        result=runtime.process("なぜ予算を増額するのでしょうか。")
        state=runtime.snapshot()
        self.assertEqual(result["question_count"],1)
        self.assertEqual(result["interest_count"],1)
        self.assertNotIn("actions",state)

    def test_filler_is_not_promoted_to_claim(self):
        runtime=StreamingContextRuntime()
        result=runtime.process("えー")
        self.assertEqual(result["claim_count"],0)
        self.assertEqual(result["events"][0]["kind"],"FILLER")


if __name__=="__main__":
    unittest.main()
