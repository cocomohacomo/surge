# Minimal Projection Admission v0.1
This tranche adds a durable journal-backed ingress boundary for the minimal
three-field projection, separate from the legacy RecognitionPacket /
ProjectionBatch / SemanticDelta path.

## Behavior
- Validates information and operational identity fields.
- Accepts a concrete target cluster or explicit no-target state.
- Rejects non-finite distances without assigning polarity or scale.
- Persists projector profile identity and the inventory revision used.
- Rejects conflicting projection IDs and idempotency-key reuse.
- Rebuilds the accepted projection index from committed journal events.

## Non-goals
This layer does not verify semantic nearest-neighbor correctness, resolve stale
cluster IDs, create or merge clusters, or mutate ContextGraph. Those operations
require a versioned cluster inventory and a separately specified growth
transaction. This journaled projection is the durable input to that next step.

Distance is opaque data and is never used here to choose, merge, split, or
create a cluster.
