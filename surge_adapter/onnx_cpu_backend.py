"""Optional ONNX Runtime CPU backend for compact SURGE recognizers."""
from __future__ import annotations
from typing import Any, Mapping, Protocol, Sequence
from surge_adapter.compact_recognition import CompactModelSpec, RecognitionCandidate

class OnnxSessionPort(Protocol):
    def get_inputs(self) -> Sequence[Any]: ...
    def get_outputs(self) -> Sequence[Any]: ...
    def run(self, output_names: list[str] | None, input_feed: Mapping[str, Any]) -> Sequence[Any]: ...
class CompactTokenizer(Protocol):
    def encode(self, text: str) -> Mapping[str, Any]: ...
class CandidateDecoder(Protocol):
    def decode(self, text: str, encoded: Mapping[str, Any], outputs: Mapping[str, Any]) -> Sequence[RecognitionCandidate]: ...

def _default_session_factory(model_path: str, intra_op_num_threads: int) -> OnnxSessionPort:
    try:
        import onnxruntime as ort
    except ImportError as exc:
        raise RuntimeError("ONNXRUNTIME_NOT_INSTALLED: install surge[onnx-cpu]") from exc
    options=ort.SessionOptions()
    options.intra_op_num_threads=intra_op_num_threads
    options.inter_op_num_threads=1
    options.execution_mode=ort.ExecutionMode.ORT_SEQUENTIAL
    options.graph_optimization_level=ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    return ort.InferenceSession(model_path,sess_options=options,providers=["CPUExecutionProvider"])

class CompactOnnxCpuBackend:
    def __init__(self,spec:CompactModelSpec,tokenizer:CompactTokenizer,decoder:CandidateDecoder,*,intra_op_num_threads:int=1,session_factory=None):
        if spec.device!="CPU": raise ValueError("ONNX_CPU_BACKEND_REQUIRES_CPU_PROFILE")
        if isinstance(intra_op_num_threads,bool) or not isinstance(intra_op_num_threads,int) or intra_op_num_threads<1:
            raise ValueError("ONNX_THREAD_COUNT_INVALID")
        spec.verify_artifact()
        self.spec,self.tokenizer,self.decoder=spec,tokenizer,decoder
        self.session=(session_factory or _default_session_factory)(spec.artifact_path,intra_op_num_threads)
        self.input_names=tuple(x.name for x in self.session.get_inputs())
        self.output_names=tuple(x.name for x in self.session.get_outputs())
        if not self.input_names or not self.output_names: raise ValueError("ONNX_MODEL_IO_EMPTY")
    def recognize(self,text:str,context:dict[str,Any]|None=None)->Sequence[RecognitionCandidate]:
        encoded=self.tokenizer.encode(text)
        if not isinstance(encoded,Mapping): raise ValueError("TOKENIZER_OUTPUT_INVALID")
        missing=[name for name in self.input_names if name not in encoded]
        if missing: raise ValueError("ONNX_INPUT_MISSING:"+",".join(missing))
        feed={name:encoded[name] for name in self.input_names}
        values=self.session.run(list(self.output_names),feed)
        if len(values)!=len(self.output_names): raise ValueError("ONNX_OUTPUT_COUNT_MISMATCH")
        outputs=dict(zip(self.output_names,values))
        candidates=tuple(self.decoder.decode(text,encoded,outputs))
        if not all(isinstance(x,RecognitionCandidate) for x in candidates): raise ValueError("ONNX_DECODER_CANDIDATE_TYPE_INVALID")
        if any(x.end_offset>len(text.encode("utf-8")) for x in candidates): raise ValueError("ONNX_DECODER_OFFSET_OUT_OF_RANGE")
        return candidates
    def warmup(self,text:str="これはモデルのウォームアップです")->None:
        self.recognize(text,{"warmup":True})
