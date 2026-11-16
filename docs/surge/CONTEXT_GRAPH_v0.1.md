# SURGE Context Graph v0.1

## Purpose

Provide the first typed semantic state layer over EventJournal. The graph is scoped to one Context. A SemanticDelta is validated against a staged copy, serialized as one journal transaction, then published to in-memory state only after the journal commit succeeds. Restart recovery replays committed records.

## Records

Node kinds: CLUSTER, ENTITY, EVIDENCE, CLAIM, QUESTION, QUESTION_ASPECT, DEPENDENCY, INTEREST.

Relation kinds: SUPPORTS, CONTRADICTS, AGREES_WITH, CONFLICTS_WITH, DEPENDS_ON,
ABOUT, EVIDENCE_FOR, PUBLISHED_BY.

Typed reference rules:
- CLAIM references one or more EVIDENCE nodes.
- EVIDENCE has no generic references; provenance is represented by typed
  EVIDENCE_FOR (EVIDENCE -> CLAIM) and PUBLISHED_BY (EVIDENCE -> ENTITY)
  relations.
- QUESTION_ASPECT references exactly one QUESTION.
- INTEREST references exactly one QUESTION_ASPECT and one target CLUSTER.
- DEPENDENCY references exactly two distinct existing nodes.
- FOCUS targets a CLUSTER and may optionally target an INTEREST whose cluster reference matches.

Relation constraints:
- SUPPORTS/CONTRADICTS connect CLAIM to CLAIM.
- AGREES_WITH/CONFLICTS_WITH connect CLUSTER to CLUSTER or CLAIM to CLAIM.
- ABOUT connects CLAIM/QUESTION/QUESTION_ASPECT to ENTITY/CLUSTER.
- EVIDENCE_FOR connects EVIDENCE to CLAIM.
- PUBLISHED_BY connects EVIDENCE to ENTITY.
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
