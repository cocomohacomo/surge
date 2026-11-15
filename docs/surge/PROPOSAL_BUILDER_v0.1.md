# SURGE ClassificationProposal Builder v0.1

## Purpose
The builder maps packet lineage, typed recognition candidates and the
Recognition Scoring v0.1 result into the existing v0.9
ClassificationProposal contract. It is a pure adapter, not a Core component.

## Mapping
- packet_id -> packet_ref
- idempotency_key -> idempotency_key
- source_generation/source_sequence -> packet source lineage
- context_hint.state_version -> context_hint_state_version
- caller-supplied base_state_version -> proposal read version
- candidate label family/label -> proposal label family/label
- selected score dimension -> label score, with scale 0..1
- candidate spans -> absolute half-open offsets using the packet offset basis

The caller must explicitly choose which score dimension backs each label.
No hidden weighted aggregation is performed.

## Abstention and review
LOW_CONFIDENCE and AMBIGUOUS_REFERENCE map to the existing proposal-schema
LOW_MARGIN abstention reason. Detailed reasons and scoring decision are also
preserved in warnings. REVIEW adds MANUAL_REVIEW_REQUIRED. These fields remain
untrusted proposal metadata; Core must not commit abstained candidates as
resolved assertions.

## Validation and limits
The builder validates the complete proposal against
schemas/surge-v0.9-proposal.schema.json and rejects candidate spans outside
the packet range or with a mismatched offset basis. It does not establish
that the label is semantically correct, resolve cross-packet scopes, validate
causal predecessors, or authorize a SemanticDelta. Those remain Core duties.
