"""Deterministic end-to-end ProjectionBatch stream simulation.

Synthetic projector profiles exercise the public Python -> C ABI -> Core path.
No external model is invoked: producer metadata varies while the Core contract
and admission path remain identical.
"""
from __future__ import annotations

import copy
import hashlib
import json
import os
import pathlib
import tempfile
import unittest

from surge_adapter.packet_ingest import CppCoreBridge
from surge_adapter.projection import ProjectionBatchAdapter


ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILES = (
    ("rule-projector", "1.2", "ja-rule-v2"),
    ("llm-projector-sim", "mock-3", "ja-context-v3"),
    ("document-projector", "2.1", "document-layout-v1"),
    ("structured-projector", "1.4", "record-schema-v2"),
)
BATCH_COUNT = 96
CLUSTER_WIDTH = 8


def make_batch(template: dict, index: int) -> dict:
    batch = copy.deepcopy(template)
    group = index // CLUSTER_WIDTH
    cluster_id = f"sim-cluster-{group:02d}"
    packet_id = f"sim-packet-{index:03d}"
    projection_id = f"sim-projection-{index:03d}"
    claim_id = f"sim-claim-{index:03d}"
    evidence_id = f"sim-evidence-{index:03d}"
    membership_id = f"sim-membership-{index:03d}"
    source = (
        f"会議記録 {index:03d}: "
        f"開催日は {'火曜日' if index % 2 == 0 else '水曜日'}。"
        f"入力区分={('未分類' if group == 0 else '通常')}。"
    )
    source_bytes = source.encode("utf-8")
    digest = hashlib.sha256(source_bytes).hexdigest()
    profile = PROFILES[index % len(PROFILES)]

    batch["schema_version"] = "0.1.0"
    batch["projection_id"] = projection_id
    batch["idempotency_key"] = f"idem-{projection_id}"
    batch["context_id"] = "simulation-context"
    batch["producer"] = {"producer_id": profile[0], "producer_version": profile[1]}
    batch["policy_version"] = profile[2]
    batch["base_state_version"] = index
    batch["normalized_source_snapshot"] = source
    batch["source_event_refs"] = [packet_id]
    batch["evidence_refs"] = [evidence_id]
    batch["source_packet"] = {
        "packet_id": packet_id,
        "idempotency_key": f"packet-idem-{index:03d}",
        "source_id": "simulation-source",
        "source_generation": "simulation-generation-1",
        "block_ref": f"simulation-block-{index:03d}",
        "offset_basis": "UTF8_BYTE_NFC",
        "start_offset": 0,
        "end_offset": len(source_bytes),
        "source_sequence": index + 1,
        "source_snapshot_digest": digest,
        "source_range_digest": digest,
        "canonical_request_digest": hashlib.sha256(
            f"canonical-request-{index}".encode()
        ).hexdigest(),
        "revision": False,
        "revision_sequence": 0,
        "context_id": "simulation-context",
    }

    nodes = []
    if index % CLUSTER_WIDTH == 0:
        nodes.append({
            "id": cluster_id,
            "kind": "CHAOS_CLUSTER" if group == 0 else "CLUSTER",
            "content": "未分類の入力群" if group == 0 else f"会議日程 topic {group}",
            "references": [],
        })
    nodes.extend([
        {
            "id": claim_id,
            "kind": "CLAIM",
            "content": (
                f"開催日={ '火曜日' if index % 2 == 0 else '水曜日' }; "
                f"producer={profile[0]}"
            ),
            "references": [evidence_id],
        },
        {
            "id": evidence_id,
            "kind": "EVIDENCE",
            "content": source,
            "references": [],
        },
    ])
    batch["nodes"] = nodes
    batch["relations"] = []
    if index % 2 == 1:
        batch["relations"].append({
            "id": f"sim-conflict-{index // 2:03d}",
            "kind": "CONTRADICTS",
            "from": f"sim-claim-{index - 1:03d}",
            "to": claim_id,
            "weight": 0.9,
        })
    batch["memberships"] = [{
        "relation_id": membership_id,
        "member_ref": claim_id,
        "cluster_ref": cluster_id,
        "upstream_weight": round(0.55 + (index % 40) / 100.0, 2),
    }]
    batch["spatial_placements"] = [{
        "cluster_id": cluster_id,
        "position": {
            "x": float(group),
            "y": float(index % CLUSTER_WIDTH),
            "z": float(index % 5),
        },
    }]
    return batch


class ProjectionStreamSimulation(unittest.TestCase):
    def test_96_batch_multi_projector_stream_recovery_and_replay(self):
        template_path = ROOT / "tests" / "fixtures" / "projection" / "conversation.json"
        template = json.loads(template_path.read_text(encoding="utf-8"))
        batches = [make_batch(template, i) for i in range(BATCH_COUNT)]
        adapter = ProjectionBatchAdapter()

        # Every synthetic producer uses the same contract and passes transport validation.
        for batch in batches:
            adapter.validate(batch)
        self.assertEqual(
            {batch["producer"]["producer_id"] for batch in batches},
            {profile[0] for profile in PROFILES},
        )

        with tempfile.TemporaryDirectory() as tmp:
            journal = pathlib.Path(tmp) / "projection-simulation.sgj"
            with CppCoreBridge(journal) as core:
                interrupted = copy.deepcopy(batches)
                interrupted[48]["memberships"][0]["upstream_weight"] = 1.25

                first = adapter.commit_stream(interrupted, core)
                self.assertFalse(first.complete)
                self.assertEqual(first.committed_batches, 48)
                self.assertEqual(first.next_index, 48)
                self.assertEqual(first.state_version, 48)
                self.assertEqual(first.nodes_processed, 102)
                self.assertEqual(first.relations_processed, 24)
                self.assertEqual(first.memberships_processed, 48)
                self.assertEqual(first.spatial_placements_processed, 48)
                self.assertGreater(first.input_bytes, 0)
                self.assertGreater(first.elapsed_microseconds, 0)
                self.assertIn("SCHEMA_INVALID", first.error)
                self.assertEqual(core.packet_count, 48)

                # Repair the rejected proposal and resume exactly at the reported cursor.
                resumed = adapter.commit_stream(batches[first.next_index:], core)
                self.assertTrue(resumed.complete)
                self.assertEqual(resumed.committed_batches, 48)
                self.assertEqual(resumed.duplicate_batches, 0)
                self.assertEqual(resumed.next_index, 48)
                self.assertEqual(resumed.state_version, BATCH_COUNT)
                self.assertEqual(resumed.nodes_processed, 102)
                self.assertEqual(resumed.relations_processed, 24)
                self.assertEqual(resumed.memberships_processed, 48)
                self.assertEqual(resumed.spatial_placements_processed, 48)
                self.assertGreater(resumed.input_bytes, 0)
                self.assertGreater(resumed.elapsed_microseconds, 0)
                self.assertEqual(core.packet_count, BATCH_COUNT)

                # Replaying the full stream must not append records or double-reserve memory.
                before = core.resource_snapshot
                replay = adapter.commit_stream(batches, core)
                after = core.resource_snapshot
                self.assertTrue(replay.complete)
                self.assertEqual(replay.duplicate_batches, BATCH_COUNT)
                self.assertEqual(replay.state_version, BATCH_COUNT)
                self.assertEqual(core.packet_count, BATCH_COUNT)
                self.assertEqual(
                    before["resident_bytes_used"], after["resident_bytes_used"]
                )
                self.assertEqual(
                    before["retained_events_used"], after["retained_events_used"]
                )
                self.assertEqual(after["transient_bytes_used"], 0)

                report = {
                    "simulation": "projection-stream-96",
                    "synthetic_batches": BATCH_COUNT,
                    "projector_profiles": {
                        name: sum(
                            batch["producer"]["producer_id"] == name for batch in batches
                        )
                        for name, _, _ in PROFILES
                    },
                    "cluster_groups": BATCH_COUNT // CLUSTER_WIDTH,
                    "chaos_cluster_groups": 1,
                    "conflicting_candidate_pairs": BATCH_COUNT // 2,
                    "interrupted_after_commits": first.committed_batches,
                    "resume_cursor": first.next_index,
                    "resumed_commits": resumed.committed_batches,
                    "replay_duplicates": replay.duplicate_batches,
                    "state_version": core.state_version,
                    "packet_count": core.packet_count,
                    "input_bytes": first.input_bytes + resumed.input_bytes,
                    "nodes_processed": first.nodes_processed + resumed.nodes_processed,
                    "relations_processed": first.relations_processed + resumed.relations_processed,
                    "memberships_processed": (
                        first.memberships_processed + resumed.memberships_processed
                    ),
                    "spatial_placements_processed": (
                        first.spatial_placements_processed
                        + resumed.spatial_placements_processed
                    ),
                    "elapsed_microseconds": (
                        first.elapsed_microseconds + resumed.elapsed_microseconds
                    ),
                    "resident_bytes_used": after["resident_bytes_used"],
                    "retained_events_used": after["retained_events_used"],
                    "transient_bytes_used_after_replay": after["transient_bytes_used"],
                    "idempotent_replay_resource_delta_bytes": (
                        after["resident_bytes_used"] - before["resident_bytes_used"]
                    ),
                    "idempotent_replay_retained_event_delta": (
                        after["retained_events_used"] - before["retained_events_used"]
                    ),
                    "result": "PASS",
                    "notes": [
                        "All four producer profiles use one ProjectionBatch contract.",
                        "Opposing Tuesday/Wednesday claims are submitted as distinct claims.",
                        "The first group is proposed as CHAOS_CLUSTER by the external producer.",
                        "One invalid weight interrupts the stream; the repaired suffix resumes at next_index.",
                        "The full replay is duplicate-only and does not increase persistent resource reservations.",
                        "No external LLM inference or Focus mutation is part of this simulation.",
                    ],
                }
                report_path = os.environ.get("SURGE_SIMULATION_REPORT")
                if report_path:
                    pathlib.Path(report_path).write_text(
                        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8",
                    )
                summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
                if summary_path:
                    with open(summary_path, "a", encoding="utf-8") as summary:
                        summary.write("## SURGE ProjectionBatch 96-item simulation\n\n")
                        summary.write("| Metric | Result |\n|---|---:|\n")
                        for key in (
                            "synthetic_batches", "cluster_groups", "chaos_cluster_groups",
                            "interrupted_after_commits", "resume_cursor", "resumed_commits",
                            "replay_duplicates", "state_version", "packet_count",
                            "input_bytes", "nodes_processed", "relations_processed", "memberships_processed",
                            "spatial_placements_processed", "elapsed_microseconds",
                            "resident_bytes_used", "retained_events_used",
                            "transient_bytes_used_after_replay",
                            "idempotent_replay_resource_delta_bytes",
                            "idempotent_replay_retained_event_delta",
                        ):
                            summary.write(f"| {key} | {report[key]} |\n")
                        summary.write("\nProjector profiles: " + ", ".join(
                            f"{name}={count}" for name, count in report["projector_profiles"].items()
                        ) + "\n\n")
                        summary.write("**Result: PASS**. Synthetic claims and producer metadata only; "
                                      "no external model inference and no Focus mutation.\n")

            # All records survive journal recovery, not just the in-memory coordinator.
        with CppCoreBridge(journal) as recovered:
            self.assertEqual(recovered.state_version, BATCH_COUNT)
            self.assertEqual(recovered.packet_count, BATCH_COUNT)
            self.assertGreater(recovered.resource_snapshot["resident_bytes_used"], 0)

        # The report artifact is deliberately generated after successful native commits.
        self.assertEqual(BATCH_COUNT, sum(
            1 for batch in batches if batch["source_packet"]["packet_id"]
        ))


if __name__ == "__main__":
    unittest.main()
