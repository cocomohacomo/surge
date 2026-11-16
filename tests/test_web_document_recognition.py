import pathlib, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from surge_adapter.web_document import (
    ClaimApplicability, ClaimApplicabilityComparator, WebDocumentIndex,
    WebDocumentRecognitionEngine, split_document, temporal_state
)

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
