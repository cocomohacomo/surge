# Recognition Candidate Scoring Bridge v0.1

The bridge connects compact model candidates to the existing replaceable
`RecognitionScorer`. It binds each proposal batch to the expected packet ID
and normalized source SHA-256 before scoring.

The injected `CandidateRelationScorer` must provide relevance,
relation_strength and novelty. The candidate supplies confidence, ambiguity
and importance. Missing dimensions are rejected; no neutral or synthetic
score is invented.

ACCEPT, REVIEW and ABSTAIN are preserved as separate outcomes. ACCEPT means
eligible for a downstream proposal review stage only. Every result has
`commit_ready=false`. This bridge creates no Claim, Evidence, SemanticDelta,
Question, Cluster or EventJournal entry. Core-side scope, reference,
dependency, temporal, contradiction, retraction and State Version checks
remain authoritative.

The relation scorer is replaceable. Performance certification for RTF < 0.01
must include this stage and all downstream packet/Core work, not just model
inference.
