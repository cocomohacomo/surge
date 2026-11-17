# SURGE Context Graph v0.1

## Purpose

Provide the first typed semantic state layer over EventJournal. The graph is scoped to one Context. A SemanticDelta is validated against a staged copy, serialized as one journal transaction, then published to in-memory state only after the journal commit succeeds. Restart recovery replays committed records.

## Records

Node kinds: CLUSTER, CHAOS_CLUSTER, ENTITY, EVIDENCE, CLAIM, QUESTION, QUESTION_ASPECT, DEPENDENCY, INTEREST.

Relation kinds: SUPPORTS, CONTRADICTS, AGREES_WITH, CONFLICTS_WITH, DEPENDS_ON, ABOUT.

Typed reference rules:
- CLAIM references one or more EVIDENCE nodes.
- QUESTION_ASPECT references exactly one QUESTION.
- INTEREST references exactly one QUESTION_ASPECT and one target CLUSTER or CHAOS_CLUSTER.
- DEPENDENCY references exactly two distinct existing nodes.
- FOCUS targets a typed CLUSTER or CHAOS_CLUSTER and may optionally target an INTEREST whose target reference matches. The target kind is explicit and a Chaos Cluster is never implicitly promoted or converted to a topic CLUSTER.

Relation constraints:
- SUPPORTS/CONTRADICTS connect CLAIM to CLAIM.
- AGREES_WITH/CONFLICTS_WITH connect CLUSTER to CLUSTER or CLAIM to CLAIM.
- ABOUT connects CLAIM/QUESTION/QUESTION_ASPECT to ENTITY/CLUSTER.
- DEPENDS_ON connects any two distinct existing nodes.

Nodes and relations are append-only. This first version does not implement merge, split, delete or tombstone semantics. Entity identity resolution remains a separate Core operation.

## Delta transaction

- Delta ID, idempotency key and expected state version are mandatory.
- Node references must resolve to existing nodes or nodes earlier in the same delta.
- Relation endpoints must resolve after node operations.
- All operations are validated against staged state before persistence.
- EventJournal commit is the atomic visibility boundary.
- Idempotent retry returns the original logical success; changed request under same key is rejected.
- Recovery replays only journal-committed event batches.

## Limitations

This is a typed graph substrate, not the full cognition engine. It does not yet calculate contradiction-to-Question routing, dependency impact sets, SCC convergence, temporal validity, trust, evidence weighting, or automated cluster assignment. It uses the local EventJournal durability and security limitations documented in EVENT_JOURNAL_v0.1.md.


## Chaos Cluster Focus extension

A Chaos Cluster can be represented in ContextGraph as a typed `CHAOS_CLUSTER` node. The dedicated C API verifies that the authoritative ChaosClusterLedger contains the target, then commits the graph projection and Focus record in one ContextGraph transaction. The graph node is a navigation projection, not a duplicate source of Chaos Cluster state. Focus serialization accepts legacy two-field records as ordinary CLUSTER targets and writes an explicit target-kind flag for new records. INTEREST may point to either target kind.
