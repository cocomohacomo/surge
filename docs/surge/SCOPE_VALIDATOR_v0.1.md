# SURGE ScopeSpan Validator v0.1

## Anchor contract

All anchors use half-open UTF8_BYTE_NFC offsets into the immutable normalized source snapshot. The validator checks source digest, offset basis, packet lineage, bounds and exact byte-slice equality. It does not infer or repair offsets.

## Type-specific rules

- NEGATION: exactly one Claim target. Negation never spreads to a second independent predicate.
- CONDITION: exactly two Claim targets, representing condition and consequent. An open span is UNRESOLVED; even a closed conditional does not emit an unconditional Claim.
- MODALITY: one Claim and an explicit modality kind, separate from factuality/polarity.
- CONCESSION / CONTRAST: two Claim targets and a closer anchor. Both sides remain represented.
- COREFERENCE / ANAPHORA: one mention target and one or more Entity antecedent candidates. Multiple candidates remain UNRESOLVED; no Entity merge occurs.
- QUOTATION: opener and closer required for closure. Attribution may be explicitly UNKNOWN; quote stance is not author stance.
- TEMPORAL: one Claim and an explicit temporal role.
- LIST_CONTINUATION: exact parent/list reference required.
- TABLE_HEADER: exact table/header reference required.

The validator returns a typed semantic effect and never directly commits graph state. The transaction layer must atomically persist the ScopeSpan revision with any resulting Claim/Relation updates.

## Limits

The initial implementation validates one immutable normalized source snapshot. It does not yet resolve anchors across independently digested source generations, infer syntactic scopes from raw text, maintain append-only ScopeSpan revisions, or integrate with ContextGraph transactions.
