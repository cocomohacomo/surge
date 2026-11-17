# Chaos Cluster Core Implementation v1.1

## Focus as an exploration target

The Core now persists one active Chaos Focus independently from ordinary topic Focus. It identifies a Chaos Cluster as the current exploration region and may carry an Interest reference, a human-readable reason and producer/policy provenance.

Focus is a retrieval-priority signal only. It does not assert that all members share a topic, and it does not upgrade any association or hypothesis epistemic status. Setting a new Chaos Focus clears the previous target in the same journal snapshot transaction. Archived clusters cannot be focused.

C++ API:
- `ChaosClusterLedger::set_focus`
- `ChaosClusterLedger::get_focus`

C ABI:
- `surge_chaos_cluster_focus_set`
- `surge_chaos_cluster_focus_get`

## Diversity-aware retrieval

The first deterministic retrieval policy uses member admission-reason groups as a coarse diversity dimension. It round-robins groups rather than returning a long run from one group, then reports:
- state version;
- selected member references and source event references;
- admission reason;
- omitted member references;
- completeness flag.

C++ API: `ChaosClusterLedger::retrieve(cluster_id, limit, diversity_first)`

C ABI: `surge_chaos_cluster_retrieve`

This is a deterministic baseline, not a learned semantic-diversity score. Admission reason is only a proxy for diversity. Future versions should add independently sourced novelty/diversity observations and task-specific ranking while preserving provenance and unknown values.

## Persistence compatibility

The snapshot codec appends an optional `CC3` extension after the existing base and `CC2` extensions. Older base and `CC2` snapshots remain readable. Focus state is reconstructed during journal replay. New snapshots include focus target, Interest reference, reason and provenance for each cluster.

## Verification

The dedicated C++ and C ABI tests cover:
- focus creation and replacement target representation;
- focus recovery after close/reopen;
- diversity-first round-robin retrieval;
- omitted-reference reporting and completeness;
- bounded C ABI JSON output.

This increment does not yet integrate Chaos Focus with QuestionRouter's Interest graph or replace the existing ContextGraph Focus API. It establishes a durable exploration-target primitive for that later integration.
