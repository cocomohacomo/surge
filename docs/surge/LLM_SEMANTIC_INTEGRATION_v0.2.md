# LLM Semantic Integration v0.2

## Recognition and authority boundary

The LLM remains an untrusted Recognition Candidate producer. It has no Core
handle, journal credentials, tools, or commit authority. Strict output
validation and the six-axis RecognitionScorer remain upstream of semantic
mutation. Only ACCEPTed Claim candidates can create resolved Claims.

## Topic cluster continuity

Within one live `LLMSemanticRuntime` session, an exact repeated topic reuses
the Cluster ID already attached to a prior accepted Claim. A new Cluster is
created only when no accepted Claim in the current session has that exact topic.
This prevents consecutive turns on the same topic from manufacturing a new
Cluster for every packet.

This is intentionally session-scoped. Cross-process cluster discovery and
topic-equivalence inference remain Core retrieval work; topic strings are not
silently normalized into semantic equivalence.

## Contradiction lifecycle

1. The runtime creates the new Claim and its ABOUT relation to the selected
   topic Cluster.
2. An opposite-polarity Claim with the same proposition creates only a
   CONTRADICTS relation in the atomic packet/semantic transaction.
3. After that transaction commits, the runtime calls the C++ Core
   `QuestionRouter` for each newly accepted contradiction relation.
4. The Core creates the durable Question, QuestionAspect and one Interest per
   distinct Cluster reached from the two Claim endpoints.
5. Focus is selected only when exactly one distinct Cluster is linked. If
   zero or multiple Clusters are linked, existing Focus is preserved.

The runtime no longer creates a parallel Question/Aspect/Interest structure
itself. Core routing is the single authority for this lifecycle. Replaying the
same relation is idempotent and does not duplicate its route.

## Failure and retry behavior

The semantic relation transaction is committed before Question routing. If
routing fails, the relation remains durable and the runtime returns the
deterministic `relation_id` plus an explicit failed route result. The caller
can retry `core.route_contradiction(relation_id)` without replaying or
re-admitting the source text. A future outbox/reconciliation worker may
automate this recovery; this adapter does not silently claim that a failed
route succeeded.

## Conformance

`tests/test_llm_semantic_integration.py` verifies:

- same-topic contradiction reuses one Cluster and routes to one Interest;
- Core Focus selects that single Cluster;
- Question routing survives restart and duplicate route attempts are
  idempotent;
- cross-topic contradiction produces multiple Interest candidates without
  guessing Focus;
- route failure returns a retryable relation ID, and a later explicit retry
  completes the route.

The tests use deterministic stub recognition providers. They do not claim live
LLM quality or model-specific recognition accuracy.
