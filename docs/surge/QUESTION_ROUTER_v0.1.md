# SURGE Question Router v0.1

## Trigger

The router accepts only an existing CONTRADICTS relation whose endpoints are both CLAIM nodes. It does not infer contradictions from raw text; Recognition/Scope validation must create the relation first.

## Transformation

For one contradiction relation:

1. Create one Question describing the pair of conflicting Claims.
2. Create one QuestionAspect scoped to checking validity, conditions and temporal context.
3. Find all distinct Clusters connected to either Claim by ABOUT relations.
4. Create one Interest per target Cluster, each referencing the QuestionAspect and that Cluster.
5. If exactly one target Cluster exists, set Focus to that Cluster and its Interest.
6. If zero or multiple target Clusters exist, do not guess a Focus. Preserve the Interest candidates and leave current Focus unchanged.

The routing delta is atomic and idempotent by contradiction relation ID. A repeated route returns the existing Question/Aspect/Interest references without creating duplicates.

## Non-goals and safeguards

- No action or answer generation is performed.
- Multiple candidate Clusters do not produce an arbitrary focus choice.
- No trust, source-growth, temporal or salience weighting is applied yet.
- Policy version is recorded in QuestionAspect content for traceability; policy registry validation remains future work.
