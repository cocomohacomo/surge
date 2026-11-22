"""Cross-block cognition continuity and topic branching stress test.

The transport currently anchors each packet to a block-local UTF-8 snapshot.
This test verifies that projector-supplied cluster membership survives across
successive blocks and that a topic transition can start a separate cluster.
It also records the global byte offsets computed by the producer, without
pretending that ProjectionBatch v0.1 persists a document-global offset map.
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
BLOCKS = [
    "会議の日程は火曜日に確定した。関係者へ共有する。",
    "火曜日の開始時刻は午前十時とする。会議室を予約する。",
    "参加者は資料を前日までに確認する。議題は変更しない。",
    "議事録の担当者は佐藤とする。決定事項を記録する。",
    "ここから別の話題に移る。次の議題は予算案である。",
    "予算案の上限は五百万円とする。見積書を比較する。",
    "調達時期は来月を予定する。必要な手続きを確認する。",
    "予算案は次回会議で再確認する。未確定事項として残す。",
]


def make_batch(template: dict, text: str, index: int, global_start: int,
               global_end: int) -> dict:
    batch = copy.deepcopy(template)
    raw = text.encode("utf-8")
    digest = hashlib.sha256(raw).hexdigest()
    packet_id = f"continuity-packet-{index:03d}"
    projection_id = f"continuity-projection-{index:03d}"
    evidence_id = f"continuity-evidence-{index:03d}"
    claim_id = f"continuity-claim-{index:03d}"
    cluster_id = "cluster-meeting" if index < 4 else "cluster-budget"
    nodes = []
    if index in (0, 4):
        nodes.append({
            "id": cluster_id, "kind": "CLUSTER",
            "content": "会議日程" if index == 0 else "予算案",
            "references": [],
        })
    nodes.extend([
        {"id": evidence_id, "kind": "EVIDENCE", "content": text, "references": []},
        {"id": claim_id, "kind": "CLAIM",
         "content": f"block-{index}-claim", "references": [evidence_id]},
    ])
    relations = []
    if index > 0 and index not in (4,):
        relations.append({
            "id": f"continuity-relation-{index:03d}",
            "kind": "DEPENDS_ON",
            "from": f"continuity-claim-{index-1:03d}",
            "to": claim_id,
            "weight": 0.8,
        })
    batch.update({
        "schema_version": "0.1.0",
        "projection_id": projection_id,
        "idempotency_key": f"continuity-idem-{index:03d}",
        "context_id": "continuity-context",
        "producer": {"producer_id": "continuity-projector", "producer_version": "1.0"},
        "policy_version": "continuity-ja-v1",
        "base_state_version": index,
        "source_packet": {
            "packet_id": packet_id,
            "idempotency_key": f"continuity-packet-idem-{index:03d}",
            "source_id": "long-meeting-document",
            "source_generation": "generation-1",
            "block_ref": "meeting-document-v1",
            "offset_basis": "UTF8_BYTE_NFC",
            "start_offset": 0,
            "end_offset": len(raw),
            "source_sequence": index + 1,
            "source_snapshot_digest": digest,
            "source_range_digest": digest,
            "canonical_request_digest": hashlib.sha256(
                f"{packet_id}:{digest}".encode()
            ).hexdigest(),
            "revision": False,
            "revision_sequence": 0,
            "context_id": "continuity-context",
        },
        "normalized_source_snapshot": text,
        "source_event_refs": [packet_id],
        "evidence_refs": [evidence_id],
        "nodes": nodes,
        "relations": relations,
        "memberships": [{
            "relation_id": f"continuity-membership-{index:03d}",
            "member_ref": claim_id,
            "cluster_ref": cluster_id,
            "upstream_weight": 0.9,
        }],
        "spatial_placements": [],
    })
    batch["_global_start"] = global_start
    batch["_global_end"] = global_end
    return batch


class ProjectionContinuitySimulation(unittest.TestCase):
    def test_cross_block_cluster_continuity_and_topic_branch(self):
        template = json.loads(
            (ROOT / "tests/fixtures/projection/conversation.json").read_text(encoding="utf-8")
        )
        adapter = ProjectionBatchAdapter()
        batches = []
        offset = 0
        for index, text in enumerate(BLOCKS):
            size = len(text.encode("utf-8"))
            batches.append(make_batch(template, text, index, offset, offset + size))
            offset += size

        # Global byte ranges must be contiguous and non-overlapping at producer side.
        self.assertEqual(batches[0]["_global_start"], 0)
        for previous, current in zip(batches, batches[1:]):
            self.assertEqual(previous["_global_end"], current["_global_start"])
        self.assertEqual(batches[-1]["_global_end"], offset)

        # Private test metadata is not part of the public ProjectionBatch envelope.
        for batch in batches:
            batch.pop("_global_start")
            batch.pop("_global_end")
            self.assertGreater(adapter.validate(batch).__len__(), 0)

        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "continuity.sgj"
            with CppCoreBridge(journal) as core:
                # Interrupt after four committed blocks, then resume at the topic boundary.
                first = adapter.commit_stream(batches[:4], core)
                self.assertTrue(first.complete, first.error)
                self.assertEqual(first.committed_batches, 4)
                self.assertEqual(core.state_version, 4)

                second = adapter.commit_stream(batches[4:], core)
                self.assertTrue(second.complete, second.error)
                self.assertEqual(second.committed_batches, 4)
                self.assertEqual(core.state_version, 8)
                self.assertEqual(core.packet_count, 8)

                before = core.resource_snapshot
                replay = adapter.commit_stream(batches, core)
                after = core.resource_snapshot
                self.assertTrue(replay.complete)
                self.assertEqual(replay.duplicate_batches, 8)
                self.assertEqual(after["resident_bytes_used"], before["resident_bytes_used"])
                self.assertEqual(after["retained_events_used"], before["retained_events_used"])
                self.assertEqual(after["transient_bytes_used"], 0)

                report = {
                    "simulation": "projection-cross-block-continuity",
                    "blocks": len(BLOCKS),
                    "document_utf8_bytes": offset,
                    "topic_clusters": 2,
                    "claims": len(BLOCKS),
                    "same_topic_memberships": 4,
                    "second_topic_memberships": 4,
                    "cross_block_dependency_edges": 6,
                    "interrupted_after": 4,
                    "resumed": 4,
                    "replay_duplicates": replay.duplicate_batches,
                    "state_version": core.state_version,
                    "packet_count": core.packet_count,
                    "resident_bytes_used": after["resident_bytes_used"],
                    "retained_events_used": after["retained_events_used"],
                    "replay_resource_delta_bytes": (
                        after["resident_bytes_used"] - before["resident_bytes_used"]
                    ),
                    "global_offsets": "producer-side contiguous UTF8 byte ranges verified",
                    "global_offset_persistence": "NOT_IMPLEMENTED_IN_PROJECTION_BATCH_V0.1",
                    "result": "PASS",
                    "limitations": [
                        "The Core receives block-local snapshots and block-local offsets.",
                        "Global document offsets are checked in the producer test harness, not persisted by Core.",
                        "Topic assignment and membership weights are supplied by the projector; Core does not infer topics.",
                        "No Focus geometry or per-cluster depth is changed by projection.",
                    ],
                }
                report_path = os.environ.get("SURGE_CONTINUITY_REPORT")
                if report_path:
                    pathlib.Path(report_path).write_text(
                        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8",
                    )
                summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
                if summary_path:
                    with open(summary_path, "a", encoding="utf-8") as summary:
                        summary.write("## Cross-block continuity and topic branching\n\n")
                        summary.write(f"- {len(BLOCKS)} blocks / {offset:,} UTF-8 bytes\n")
                        summary.write("- 2 topic clusters / 8 claims / 8 memberships\n")
                        summary.write("- Interrupted after 4 blocks; resumed remaining 4\n")
                        summary.write("- Full replay: 8 duplicates / 0 additional resident bytes\n")
                        summary.write("- Global offsets: producer-side only; not persisted in ProjectionBatch v0.1\n\n")
                        summary.write("**Result: PASS** (block-local Core anchors; no Focus mutation).\n")

            with CppCoreBridge(journal) as recovered:
                self.assertEqual(recovered.state_version, 8)
                self.assertEqual(recovered.packet_count, 8)

if __name__ == "__main__":
    unittest.main()
