# Fixed Projector Double: Core Cognition E2E v0.1

## Purpose

This test isolates SURGE Core behavior from external recognition quality. A deterministic FixedProjectorDouble supplies the same ProjectionBatch contract on every run. It is not an LLM and does not claim semantic accuracy.

## Scenario

1. Project a Tuesday meeting claim and Evidence into a Cluster with upstream membership weight 0.73.
2. Project an independent deadline Cluster with upstream membership weight 0.61.
3. Ask Core associative recall to retrieve the meeting Cluster using a caller-supplied token.
4. Verify Core did not invent Supports or AgreesWith relations.
5. Project a Wednesday correction, explicit Contradicts relation, Question and Interest while retaining Tuesday Evidence.
6. Verify both historical Evidence records remain and recall sees multiple supporting members.
7. Ask Core to create a new co-occurrence cognition Cluster from the two existing clusters and persist DerivedFrom lineage.
8. Verify the derived Cluster is recallable.
9. Append externally produced CONFIRMED feedback and verify recall ranking changes while the original upstream membership weight remains exactly 0.73.
10. Observe a spatial Focus region and verify observation does not mutate graph state.

## Responsibility boundary

The fixed external Projector supplies interpretation text and candidate records, cluster identities and membership assignments, upstream membership weights, explicit contradiction relation, Question/Interest candidates, and spatial placement.

SURGE Core supplies schema/structural validation and atomic graph commit, durable cluster membership and relation retention, associative recall over committed graph content, derived-cluster lineage validation and persistence, feedback provenance and bounded recall adjustment, and Focus observation without semantic inference.

The Core does not decide that Tuesday or Wednesday is correct, does not generate the contradiction, does not infer agreement from an acknowledgement, and does not alter producer weights.

## Expected assertions

- Both original and corrected Evidence survive.
- Upstream weights remain unchanged.
- The explicit Contradicts edge remains explicit.
- No Supports/AgreesWith edge is fabricated.
- The new co-occurrence Cluster is a distinct node with DerivedFrom lineage to both source Clusters.
- External feedback affects retrieval ranking, not source membership weights.
- Focus observation leaves State Version unchanged.

## Limitations

This is a deterministic contract/conformance test, not an evaluation of Japanese understanding. Query terms are supplied by the caller and Core recall is lexical/associative, not semantic interpretation. The test exercises graph-level Question and Interest retention, but does not claim an autonomous Question-to-Interest-to-Focus policy loop: the external Projector supplies those candidates and the test supplies the Focus geometry. It also does not prove model interchangeability across arbitrary producers; it proves that one fixed producer can use the existing Core contract end-to-end.
