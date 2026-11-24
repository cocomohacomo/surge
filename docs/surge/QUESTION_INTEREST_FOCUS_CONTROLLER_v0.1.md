# Question → Interest → Focus Controller v0.1

## Policy

The external Projector is responsible for semantic recognition and supplies explicit Question/Interest records, target Cluster IDs, active status, priority, and target spatial position. The Core controller does not derive questions, interpret text, or calculate semantic weights.

The deterministic policy:
1. filters inactive or malformed inputs;
2. chooses the highest priority active Question/Interest;
3. breaks ties by Question ID then Interest ID for reproducibility;
4. moves the FocusRegion center to the supplied target Cluster position while retaining configured region half-extents;
5. commits the FocusRegion and target position through a SemanticDelta.

If no active valid Question/Interest exists, the controller returns no decision and performs no mutation.

## Test coverage

- highest-priority candidate selection
- inactive candidate exclusion
- malformed candidate exclusion
- deterministic tie-breaking
- FocusRegion geometry from Projector-provided target position
- graph commit of Focus state
- no-decision behavior when no active candidate exists

## Boundary and limitations

This is an autonomous **control** loop over externally supplied cognition candidates, not autonomous semantic cognition. The priority is supplied by the Projector/policy input and is not Core-calculated. This initial version does not yet implement hysteresis, dwell time, interest decay, multi-focus scheduling, or automatic Question resolution. Those should be added only with tests demonstrating that they preserve Focus responsiveness and avoid oscillation.
