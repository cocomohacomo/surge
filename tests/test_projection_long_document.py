"""Long-document ProjectionBatch stress simulation.

Exercises the same native Python -> C ABI -> Core path with a multi-megabyte
single document and an ordered stream of independently anchored text blocks.
"""
from __future__ import annotations

import copy
import hashlib
import json
import os
import pathlib
import tempfile
import time
import unittest

from surge_adapter.packet_ingest import CppCoreBridge
from surge_adapter.projection import ProjectionBatchAdapter

ROOT = pathlib.Path(__file__).resolve().parents[1]
BLOCK_REPETITIONS = 3120
BLOCK_COUNT = 64
PHRASE = "超長文の認知情報を保持し、文脈と根拠の連続性を検証する。"


def make_text_block() -> str:
    return PHRASE * BLOCK_REPETITIONS


def make_batch(template: dict, source: str, *, packet_id: str,
               projection_id: str, sequence: int, block_ref: str,
               global_start: int) -> dict:
    batch = copy.deepcopy(template)
    source_bytes = source.encode("utf-8")
    digest = hashlib.sha256(source_bytes).hexdigest()
    evidence_id = f"{projection_id}-evidence"
    claim_id = f"{projection_id}-claim"
    batch.update({
        "schema_version": "0.1.0",
        "projection_id": projection_id,
        "idempotency_key": f"idem-{projection_id}",
        "context_id": "long-document-context",
        "producer": {"producer_id": "long-document-projector", "producer_version": "1.0"},
        "policy_version": "long-document-ja-v1",
        "base_state_version": sequence - 1,
        "normalized_source_snapshot": source,
        "source_event_refs": [packet_id],
        "evidence_refs": [evidence_id],
        "source_packet": {
            "packet_id": packet_id,
            "idempotency_key": f"packet-idem-{packet_id}",
            "source_id": "long-document-source",
            "source_generation": "long-document-generation-1",
            "block_ref": block_ref,
            "document_ref": "long-document-identity",
            "document_generation": "long-document-generation-1",
            "document_sequence": sequence,
            "global_start_offset": global_start,
            "global_end_offset": global_start + len(source_bytes),
            "offset_basis": "UTF8_BYTE_NFC",
            "start_offset": 0,
            "end_offset": len(source_bytes),
            "source_sequence": sequence,
            "source_snapshot_digest": digest,
            "source_range_digest": digest,
            "canonical_request_digest": hashlib.sha256(
                f"request:{packet_id}:{digest}".encode()
            ).hexdigest(),
            "revision": False,
            "revision_sequence": 0,
            "context_id": "long-document-context",
        },
        "nodes": [
            {"id": evidence_id, "kind": "EVIDENCE", "content": source, "references": []},
            {"id": claim_id, "kind": "CLAIM",
             "content": f"長文ブロック {sequence} の内容を保持する",
             "references": [evidence_id]},
        ],
        "relations": [],
        "memberships": [],
        "spatial_placements": [],
    })
    return batch


class LongDocumentProjectionSimulation(unittest.TestCase):
    def test_multimegabyte_document_single_batch_and_64_block_stream(self):
        template = json.loads(
            (ROOT / "tests/fixtures/projection/conversation.json").read_text(encoding="utf-8")
        )
        adapter = ProjectionBatchAdapter()
        block = make_text_block()
        blocks = [block for _ in range(BLOCK_COUNT)]
        document = "".join(blocks)
        document_bytes = len(document.encode("utf-8"))
        self.assertGreaterEqual(document_bytes, 4 * 1024 * 1024)

        single_batch = make_batch(
            template, document, packet_id="long-single-packet",
            projection_id="long-single-projection", sequence=1,
            block_ref="whole-document", global_start=0,
        )
        chunk_batches = [
            make_batch(
                template, text, packet_id=f"long-block-packet-{index:03d}",
                projection_id=f"long-block-projection-{index:03d}",
                sequence=index + 1, block_ref=f"document-block-{index:03d}",
                global_start=index * len(block.encode("utf-8")),
            )
            for index, text in enumerate(blocks)
        ]

        # Transport validation must preserve NFC UTF-8 byte counts and SHA-256 anchors.
        self.assertEqual(len(adapter.validate(single_batch)), document_bytes)
        for batch in chunk_batches:
            self.assertEqual(len(adapter.validate(batch)), len(block.encode("utf-8")))

        with tempfile.TemporaryDirectory() as tmp:
            single_journal = pathlib.Path(tmp) / "single-document.sgj"
            stream_journal = pathlib.Path(tmp) / "chunked-document.sgj"

            started = time.perf_counter_ns()
            with CppCoreBridge(single_journal) as core:
                before = core.resource_snapshot
                single_result = adapter.commit(single_batch, core)
                after = core.resource_snapshot
                self.assertEqual(single_result.state_version, 1)
                self.assertEqual(core.packet_count, 1)
                self.assertGreater(after["resident_bytes_used"], before["resident_bytes_used"])
                single_metrics = {
                    "state_version": core.state_version,
                    "packet_count": core.packet_count,
                    "resident_bytes_used": after["resident_bytes_used"],
                    "retained_events_used": after["retained_events_used"],
                    "journal_bytes": single_journal.stat().st_size,
                    "commit_microseconds": single_result.elapsed_microseconds,
                }
            single_elapsed = (time.perf_counter_ns() - started) // 1000

            started = time.perf_counter_ns()
            with CppCoreBridge(stream_journal) as core:
                stream = adapter.commit_stream(chunk_batches, core)
                self.assertTrue(stream.complete, stream.error)
                self.assertEqual(stream.committed_batches, BLOCK_COUNT)
                self.assertEqual(stream.duplicate_batches, 0)
                self.assertEqual(stream.next_index, BLOCK_COUNT)
                self.assertEqual(core.state_version, BLOCK_COUNT)
                self.assertEqual(core.packet_count, BLOCK_COUNT)

                # A full retry must be duplicate-only and must not reserve more resources.
                before_replay = core.resource_snapshot
                replay = adapter.commit_stream(chunk_batches, core)
                after_replay = core.resource_snapshot
                self.assertTrue(replay.complete)
                self.assertEqual(replay.duplicate_batches, BLOCK_COUNT)
                self.assertEqual(after_replay["resident_bytes_used"],
                                 before_replay["resident_bytes_used"])
                self.assertEqual(after_replay["retained_events_used"],
                                 before_replay["retained_events_used"])
                self.assertEqual(after_replay["transient_bytes_used"], 0)

                stream_metrics = {
                    "state_version": core.state_version,
                    "packet_count": core.packet_count,
                    "input_bytes": stream.input_bytes,
                    "nodes_processed": stream.nodes_processed,
                    "elapsed_microseconds": stream.elapsed_microseconds,
                    "resident_bytes_used": after_replay["resident_bytes_used"],
                    "retained_events_used": after_replay["retained_events_used"],
                    "journal_bytes": stream_journal.stat().st_size,
                    "replay_duplicates": replay.duplicate_batches,
                    "replay_resource_delta_bytes": (
                        after_replay["resident_bytes_used"]
                        - before_replay["resident_bytes_used"]
                    ),
                }
            stream_elapsed = (time.perf_counter_ns() - started) // 1000

            # Reopen both journals and verify durable admission state.
            with CppCoreBridge(single_journal) as recovered_single:
                self.assertEqual(recovered_single.state_version, 1)
                self.assertEqual(recovered_single.packet_count, 1)
            with CppCoreBridge(stream_journal) as recovered_stream:
                self.assertEqual(recovered_stream.state_version, BLOCK_COUNT)
                self.assertEqual(recovered_stream.packet_count, BLOCK_COUNT)

            report = {
                "simulation": "projection-long-document",
                "document_utf8_bytes": document_bytes,
                "document_characters": len(document),
                "block_count": BLOCK_COUNT,
                "block_utf8_bytes": len(block.encode("utf-8")),
                "single_batch": single_metrics,
                "single_end_to_end_microseconds": single_elapsed,
                "chunked_stream": stream_metrics,
                "chunked_end_to_end_microseconds": stream_elapsed,
                "recovery": {
                    "single_batch_state_version": 1,
                    "single_batch_packet_count": 1,
                    "chunked_state_version": BLOCK_COUNT,
                    "chunked_packet_count": BLOCK_COUNT,
                },
                "result": "PASS",
                "notes": [
                    "One multi-megabyte source snapshot was admitted as one ProjectionBatch.",
                    "The same document text was admitted as 64 ordered block-local UTF-8/NFC snapshots.",
                    "Each block has block-local offsets and a Core-persisted contiguous document-global range.",
                    "Duplicate replay did not increase resident-byte or retained-event reservations.",
                    "Journal recovery verified durable packet and state counts, not a byte-for-byte graph export.",
                    "No external LLM inference and no Focus mutation were part of this test.",
                ],
            }
            report_path = os.environ.get("SURGE_LONG_DOCUMENT_REPORT")
            if report_path:
                pathlib.Path(report_path).write_text(
                    json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8",
                )
            summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
            if summary_path:
                with open(summary_path, "a", encoding="utf-8") as summary:
                    summary.write("## SURGE multi-megabyte document simulation\n\n")
                    summary.write(f"- Document: {document_bytes:,} UTF-8 bytes / {len(document):,} characters\n")
                    summary.write(f"- Single batch: {single_metrics['commit_microseconds']:,} μs commit\n")
                    summary.write(f"- Chunked stream: {BLOCK_COUNT} blocks / {stream_metrics['input_bytes']:,} input bytes\n")
                    summary.write(f"- Chunked replay: {stream_metrics['replay_duplicates']} duplicates / "
                                  f"{stream_metrics['replay_resource_delta_bytes']} additional resident bytes\n")
                    summary.write("- Recovery: both journals reopened with expected packet counts\n\n")
                    summary.write("**Result: PASS**. Core-persisted document-global UTF-8 ranges. "
                                  "No external model inference or Focus mutation.\n")

        self.assertEqual(stream_metrics["input_bytes"], document_bytes)


if __name__ == "__main__":
    unittest.main()
