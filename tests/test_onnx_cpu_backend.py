import pathlib,sys,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]; sys.path.insert(0,str(ROOT))
from surge_adapter.compact_recognition import CompactModelSpec,RecognitionCandidate
from surge_adapter.onnx_cpu_backend import CompactOnnxCpuBackend
class Item:
    def __init__(self,name): self.name=name
class Session:
    def __init__(self,inputs=("input_ids","attention_mask"),outputs=("logits",)):
        self.inputs=[Item(x) for x in inputs]; self.outputs=[Item(x) for x in outputs]; self.calls=[]
    def get_inputs(self): return self.inputs
    def get_outputs(self): return self.outputs
    def run(self,names,feed): self.calls.append((names,feed)); return [[.1,.9] for _ in names]
class Tokenizer:
    def encode(self,text): return {"input_ids":[1,2],"attention_mask":[1,1],"extra":[0]}
class Decoder:
    def decode(self,text,encoded,outputs):
        return [RecognitionCandidate("CLAIM","予算","予算を確保する","POSITIVE",False,.95,.1,.9,0,len(text.encode()))]
def spec(path,device="CPU"): return CompactModelSpec.inspect("compact-ja","1",str(path),runtime_id="onnxruntime",device=device)
class Tests(unittest.TestCase):
    def test_cpu_inference_only_feeds_declared_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"m.onnx"; p.write_bytes(b"model"); session=Session()
            backend=CompactOnnxCpuBackend(spec(p),Tokenizer(),Decoder(),session_factory=lambda path,threads:session)
            self.assertEqual(backend.recognize("予算")[0].kind,"CLAIM")
            self.assertEqual(set(session.calls[0][1]),{"input_ids","attention_mask"})
    def test_explicit_warmup(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"m.onnx"; p.write_bytes(b"model"); session=Session()
            CompactOnnxCpuBackend(spec(p),Tokenizer(),Decoder(),session_factory=lambda path,threads:session).warmup()
            self.assertEqual(len(session.calls),1)
    def test_missing_graph_input_rejected(self):
        class BadTokenizer:
            def encode(self,text): return {"input_ids":[1]}
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"m.onnx"; p.write_bytes(b"model")
            backend=CompactOnnxCpuBackend(spec(p),BadTokenizer(),Decoder(),session_factory=lambda path,threads:Session())
            with self.assertRaisesRegex(ValueError,"ONNX_INPUT_MISSING"): backend.recognize("x")
    def test_empty_graph_io_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"m.onnx"; p.write_bytes(b"model")
            with self.assertRaisesRegex(ValueError,"ONNX_MODEL_IO_EMPTY"):
                CompactOnnxCpuBackend(spec(p),Tokenizer(),Decoder(),session_factory=lambda path,threads:Session(inputs=()))
    def test_gpu_profile_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"m.onnx"; p.write_bytes(b"model")
            with self.assertRaisesRegex(ValueError,"ONNX_CPU_BACKEND_REQUIRES_CPU_PROFILE"):
                CompactOnnxCpuBackend(spec(p,"GPU"),Tokenizer(),Decoder(),session_factory=lambda path,threads:Session())
    def test_bad_thread_count_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"m.onnx"; p.write_bytes(b"model")
            with self.assertRaisesRegex(ValueError,"ONNX_THREAD_COUNT_INVALID"):
                CompactOnnxCpuBackend(spec(p),Tokenizer(),Decoder(),intra_op_num_threads=0,session_factory=lambda path,threads:Session())
    def test_decoder_offsets_are_bounded(self):
        class Bad:
            def decode(self,text,encoded,outputs):
                return [RecognitionCandidate("CLAIM",None,"bad","POSITIVE",False,.9,.1,.9,0,999)]
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"m.onnx"; p.write_bytes(b"model")
            backend=CompactOnnxCpuBackend(spec(p),Tokenizer(),Bad(),session_factory=lambda path,threads:Session())
            with self.assertRaisesRegex(ValueError,"ONNX_DECODER_OFFSET_OUT_OF_RANGE"): backend.recognize("日本語")
    def test_wrong_decoder_type_rejected(self):
        class Bad:
            def decode(self,text,encoded,outputs): return [{"kind":"CLAIM"}]
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"m.onnx"; p.write_bytes(b"model")
            backend=CompactOnnxCpuBackend(spec(p),Tokenizer(),Bad(),session_factory=lambda path,threads:Session())
            with self.assertRaisesRegex(ValueError,"ONNX_DECODER_CANDIDATE_TYPE_INVALID"): backend.recognize("text")
if __name__=="__main__": unittest.main()
