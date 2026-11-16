import pathlib, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.web_document import (
    ClaimApplicability, ClaimApplicabilityComparator, WebDocumentIndex,
    WebDocumentRecognitionEngine, split_document, temporal_state
)
from surge_adapter.packet_ingest import Admission

class Provider:
    def __init__(self, values): self.values=list(values)
    def recognize(self, text, context=None): return self.values.pop(0)

def candidate(proposition="会議を開催", polarity="POSITIVE", conditional=False):
    return {"kind":"CLAIM","topic":"会議","proposition":proposition,"polarity":polarity,
        "conditional":conditional,"confidence":.96,"ambiguity":.05,"importance":.8}

class WebDocumentRecognitionTests(unittest.TestCase):
    def test_temporal_lifecycle_states_are_distinct(self):
        self.assertEqual(temporal_state("開催を予定しています"),"PLANNED")
        self.assertEqual(temporal_state("現在検討中です"),"UNDER_REVIEW")
        self.assertEqual(temporal_state("開催しました"),"COMPLETED")
        self.assertEqual(temporal_state("計画を中止しました"),"CANCELLED")
        self.assertEqual(temporal_state("制度の廃止を予定しています"),"PLANNED")
        self.assertEqual(temporal_state("制度を廃止しました"),"CANCELLED")
        self.assertEqual(temporal_state("出荷制限の解除について"),"DECIDED")
        self.assertEqual(temporal_state("出荷制限の設定について"),"DECIDED")

    def test_applicability_comparator_blocks_false_conflicts(self):
        planned=ClaimApplicability("会議を開催","POSITIVE","防災",("災害時",),
            "岩手県","PLANNED",modality="PROSPECTIVE")
        cancelled=ClaimApplicability("会議を開催","NEGATIVE","防災",("平常時",),
            "岩手県","CANCELLED",modality="ASSERTED")
        result=ClaimApplicabilityComparator.compare(planned,cancelled)
        self.assertFalse(result.conflict)
        self.assertFalse(result.scopes_overlap)

    def test_opposite_polarity_conflicts_only_when_scopes_overlap(self):
        a=ClaimApplicability("施設を開放","POSITIVE","施設",("災害時",),"A市","CURRENT")
        b=ClaimApplicability("施設を開放","NEGATIVE","施設",("災害時",),"A市","CURRENT")
        self.assertTrue(ClaimApplicabilityComparator.compare(a,b).conflict)
        c=ClaimApplicability("施設を開放","NEGATIVE","施設",("平常時",),"A市","CURRENT")
        self.assertFalse(ClaimApplicabilityComparator.compare(a,c).conflict)

    def test_long_document_chunks_keep_utf8_byte_offsets_and_overlap(self):
        text=("日本語の長文です。"*1800)
        chunks=split_document(text,max_chars=800,overlap_chars=80)
        self.assertGreater(len(chunks),2)
        for chunk in chunks:
            raw=text.encode("utf-8")
            self.assertEqual(raw[chunk.start_byte:chunk.end_byte].decode("utf-8"),chunk.text)
        self.assertTrue(any(a.end_char>b.start_char for a,b in zip(chunks,chunks[1:])))

    def test_web_prepare_emits_provenance_temporal_and_typed_scopes(self):
        with tempfile.TemporaryDirectory() as tmp:
            index=WebDocumentIndex(pathlib.Path(tmp)/"web.sqlite")
            engine=WebDocumentRecognitionEngine(Provider([candidate()]),"ctx","web",index)
            text="2026年11月20日に会議を開催する予定です。開催しません。"
            result=engine.prepare(text,"https://example.jp/news?id=1","発信機関",
                                  "会議開催のお知らせ","2026-11-16T12:00:00+09:00")
            self.assertEqual(result["status"],"NEW")
            self.assertEqual(result["chunk_count"],1)
            self.assertTrue(any(n["kind"]=="EVIDENCE" and "example.jp" in n["content"]
                                for n in result["delta"]["nodes"]))
            kinds={s["kind"] for s in result["delta"]["scope_spans"]}
            self.assertIn("NEGATION",kinds)
            self.assertIn("MODALITY",kinds)
            self.assertIn("TEMPORAL",kinds)
            temporal=next(s for s in result["delta"]["scope_spans"] if s["kind"]=="TEMPORAL")
            raw=text.encode("utf-8")
            self.assertEqual(raw[temporal["opener"]["start_offset"]:
                                 temporal["opener"]["end_offset"]].decode(),
                             temporal["opener"]["exact_text"])
            index.close()

    def test_quote_attribution_is_not_confused_with_publisher(self):
        with tempfile.TemporaryDirectory() as tmp:
            index=WebDocumentIndex(pathlib.Path(tmp)/"quote.sqlite")
            text="気象庁によると「大雨の恐れがある」と発表した。"
            engine=WebDocumentRecognitionEngine(Provider([candidate()]),"ctx","web",index)
            result=engine.prepare(text,"https://example.jp/weather","報道機関")
            quote=next(s for s in result["delta"]["scope_spans"] if s["kind"]=="QUOTATION")
            self.assertTrue(quote["attribution_ref"])
            entity=next(n for n in result["delta"]["nodes"] if n["id"]==quote["attribution_ref"])
            self.assertIn("気象庁",entity["content"])
            index.close()

    def test_cross_document_opposite_claim_creates_conflict_only_for_same_scope(self):
        with tempfile.TemporaryDirectory() as tmp:
            index=WebDocumentIndex(pathlib.Path(tmp)/"cross.sqlite")
            old_app=ClaimApplicability("会議を開催","POSITIVE","会議",(),"","UNKNOWN")
            index.record("https://example.jp/old","a"*64,"old-packet",0,"old","機関",
                [{"signature":"old-signature","claim_id":"claim/old",
                  "applicability":__import__("dataclasses").asdict(old_app),
                  "source_url":"https://example.jp/old"}])
            engine=WebDocumentRecognitionEngine(Provider([
                candidate(proposition="会議を開催",polarity="NEGATIVE")]),
                "ctx","web",index)
            result=engine.prepare("会議を開催する。","https://example.jp/new","機関")
            conflicts=[r for r in result["delta"]["relations"] if r["kind"]=="CONTRADICTS"]
            self.assertEqual(len(conflicts),1)
            self.assertEqual(conflicts[0]["from"],"claim/old")
            index.close()

    def test_claim_identity_keeps_geographically_distinct_applicability(self):
        with tempfile.TemporaryDirectory() as tmp:
            index=WebDocumentIndex(pathlib.Path(tmp)/"geography.sqlite")
            first_engine=WebDocumentRecognitionEngine(Provider([candidate()]),"ctx","web",index)
            first=first_engine.prepare("盛岡市で会議を開催する。","https://example.jp/morioka","機関")
            index.record(first["url"],first["digest"],first["packet"]["packet_id"],0,
                         "盛岡","機関",first["claims"])
            second_engine=WebDocumentRecognitionEngine(Provider([candidate()]),"ctx","web",index)
            second=second_engine.prepare("仙台市で会議を開催する。","https://example.jp/sendai","機関")
            self.assertNotEqual(first["claims"][0]["signature"],second["claims"][0]["signature"])
            self.assertNotEqual(first["claims"][0]["claim_id"],second["claims"][0]["claim_id"])
            index.close()

    def test_ingest_connects_packet_and_semantic_delta_atomically(self):
        class FakeCore:
            state_version=0
            calls=0
            def admit_and_apply(self,packet,source,snapshot_digest,range_digest,
                                request_digest,revision,revision_sequence,revision_ref,delta):
                self.calls+=1
                admission=Admission("COMMITTED",packet["packet_id"],packet["context_id"],
                    packet["source_id"],packet["source_position"]["source_generation"],
                    packet["source_position"]["source_sequence"],"range",range_digest,request_digest)
                self.state_version+=1
                return admission,{"committed":True,"state_version":self.state_version}
        with tempfile.TemporaryDirectory() as tmp:
            index=WebDocumentIndex(pathlib.Path(tmp)/"ingest.sqlite")
            engine=WebDocumentRecognitionEngine(Provider([candidate()]),"ctx","web",index)
            core=FakeCore()
            first=engine.ingest("会議を開催します。","https://example.jp/a","機関",core,
                                "会議開催","2026-11-16T12:00:00+09:00")
            self.assertTrue(first["semantic_commit"]["committed"])
            self.assertEqual(core.calls,1)
            second=engine.ingest("会議を開催します。","https://example.jp/a","機関",core,
                                 "会議開催","2026-11-16T12:00:00+09:00")
            self.assertEqual(second["status"],"DUPLICATE")
            self.assertEqual(core.calls,1)
            index.close()

    def test_web_scoring_preserves_review_and_abstain(self):
        with tempfile.TemporaryDirectory() as tmp:
            index=WebDocumentIndex(pathlib.Path(tmp)/"scores.sqlite")
            review=WebDocumentRecognitionEngine(Provider([candidate(confidence=.75)]),
                "ctx","web",index).prepare("会議を開催する。","https://example.jp/review","機関")
            self.assertEqual(review["decisions"][0]["decision"],"REVIEW")
            self.assertEqual(len(review["claims"]),1)
            abstain=WebDocumentRecognitionEngine(Provider([candidate(confidence=.40)]),
                "ctx","web",index).prepare("会議を開催する。","https://example.jp/abstain","機関")
            self.assertEqual(abstain["decisions"][0]["decision"],"ABSTAIN")
            self.assertEqual(len(abstain["claims"]),0)
            self.assertFalse(any(n["kind"]=="CLAIM" for n in abstain["delta"]["nodes"]))
            index.close()

    def test_changed_capture_uses_new_immutable_source_generation(self):
        with tempfile.TemporaryDirectory() as tmp:
            index=WebDocumentIndex(pathlib.Path(tmp)/"generation.sqlite")
            url="https://example.jp/revised"
            old_text="会議を開催する。"
            old_digest=__import__("hashlib").sha256(old_text.encode()).hexdigest()
            index.record(url,old_digest,"packet-old",0,"旧題名","機関",[])
            engine=WebDocumentRecognitionEngine(Provider([candidate()]),"ctx","web",index)
            new_text="会議を開催しません。"
            result=engine.prepare(new_text,url,"機関")
            self.assertEqual(result["status"],"UPDATE")
            self.assertEqual(result["packet"]["finalization"],"FINAL")
            self.assertNotEqual(result["packet"]["source_position"]["source_generation"],
                                "web-"+old_digest[:16])
            evidence=next(n for n in result["delta"]["nodes"] if n["kind"]=="EVIDENCE")
            self.assertIn("packet-old",evidence["content"])
            self.assertIn('"document_revision": 1',evidence["content"])
            index.close()

    def test_url_duplicate_and_update_classification(self):
        with tempfile.TemporaryDirectory() as tmp:
            index=WebDocumentIndex(pathlib.Path(tmp)/"web.sqlite")
            url="https://example.jp/a"
            first="本文A"
            digest=__import__("hashlib").sha256(first.encode()).hexdigest()
            index.record(url,digest,"packet-1",0,"題名","発信元",[])
            self.assertEqual(index.inspect(url,digest)["status"],"DUPLICATE")
            changed="本文B"
            changed_digest=__import__("hashlib").sha256(changed.encode()).hexdigest()
            update=index.inspect(url,changed_digest)
            self.assertEqual(update["status"],"UPDATE")
            self.assertEqual(update["previous_packet_id"],"packet-1")
            self.assertEqual(update["revision"],1)
            index.close()

if __name__=="__main__": unittest.main()
