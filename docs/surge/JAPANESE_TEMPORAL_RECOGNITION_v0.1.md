# Japanese Temporal Recognition v0.1

Status: Conservative adapter contract

## Purpose

Extract Japanese natural-language temporal expressions into untrusted
Claim-validity candidates. The recognizer is model-independent and has no
authority to mutate the ContextGraph. A candidate must be checked against
Evidence, source offsets, Claim identity and Core's RFC3339 validation before
it can become a SemanticDelta ClaimValidity operation.

## Output

Each candidate contains the exact matched text, role
(INTERVAL, VALID_FROM or VALID_UNTIL), status, optional RFC3339 boundaries,
precision, confidence, reason and a correction flag. Status values are
RESOLVED, NEEDS_TIMEZONE, NEEDS_REFERENCE, NEEDS_YEAR and INVALID.

## Supported Japanese patterns

- `2026年4月1日から有効`: VALID_FROM at local midnight when the caller
  supplies an explicit source timezone.
- `2026年3月末で失効`: exclusive VALID_UNTIL at April 1 midnight.
- `2026年4月以降`: VALID_FROM at April 1 midnight.
- `2025年度中`: April 1, 2025 through exclusive April 1, 2026.
- `本日から30日間`: relative interval from an explicitly supplied,
  timezone-aware reference datetime.
- Correction phrases such as `ではなく` flag candidates as corrections;
  prior Claims and validity revisions are never rewritten.

## Safety and ambiguity rules

1. No local timezone is inferred. Date-only boundaries require timezone_offset
   (for example `+09:00`) or an aware reference datetime.
2. Relative expressions require an aware reference datetime. The system clock
   is never consulted.
3. Month expressions without a year return NEEDS_YEAR. The recognizer does not
   guess a year from the current date or document creation date.
4. Invalid calendar dates return INVALID and no interval.
5. Correction detection is a review signal, not an automatic supersession.
6. This first version deliberately does not resolve vague terms such as
   `近日`, `来月`, `年度末`, `上旬`, `以内`, holidays or business days.
7. Confidence is a deterministic rule-based heuristic, not a calibrated
   probability and must not be used as an acceptance threshold.

## API

`recognize_temporal_expressions(text, reference_datetime=None,
timezone_offset=None) -> list[TemporalCandidate]`

The packet helper recognize_temporal_spans emits TEMPORAL_CUE proposal spans
with absolute half-open UTF-8 byte offsets over the packet NFC snapshot.
LLMSemanticRuntime.process includes these cues in its non-committing
ClassificationProposal and returns candidate records for review. Callers may
supply reference_datetime and timezone_offset; neither is inferred. A cue does
not itself bind a temporal candidate to a Claim.

The caller retains source offsets and Evidence provenance. Before committing,
it must bind the candidate to a Claim and Evidence, validate the interval, and
append a new ClaimValidity revision with the next revision number. An unresolved
candidate must remain a Question/Interest or review item, not be coerced into
a timestamp.

## Non-goals

No automatic Claim extraction, Evidence creation, timezone guessing, date
normalization from untrusted external metadata, Core mutation, or natural
language model dependency is included.
