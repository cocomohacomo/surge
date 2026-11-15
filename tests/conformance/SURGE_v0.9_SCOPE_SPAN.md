# SURGE v0.9 ScopeSpan Conformance

| ID | Kind | Required positive case | Required negative / unresolved case |
|---|---|---|---|
| S01 | NEGATION | One anchored Claim target | Multiple independent Claim targets rejected |
| S02 | CONDITION | Two Claim targets and closer | Missing closer remains UNRESOLVED; no unconditional Claim |
| S03 | MODALITY | One Claim and explicit modality kind | Missing modality kind rejected |
| S04 | CONCESSION | Two Claims and closer | Missing closer remains UNRESOLVED |
| S05 | CONTRAST | Two Claims and closer | Missing closer remains UNRESOLVED |
| S06 | COREFERENCE | One antecedent candidate | Multiple candidates remain UNRESOLVED |
| S07 | ANAPHORA | One antecedent candidate | Ambiguous candidate set remains UNRESOLVED |
| S08 | QUOTATION | Opener/closer and optional attribution | Missing closer remains UNRESOLVED; unknown attribution is allowed |
| S09 | TEMPORAL | One Claim and temporal role | Missing role rejected |
| S10 | LIST_CONTINUATION | Parent/list reference | Missing parent rejected |
| S11 | TABLE_HEADER | Table/header reference | Missing header rejected |
| S12 | ANCHOR SAFETY | Exact normalized UTF-8 byte slice | Wrong text, wrong digest, invalid order or missing packet lineage rejected |

All cases are validated against an immutable normalized source snapshot. Digest syntax/consistency is checked; recomputation of the trusted source digest belongs to the source adapter.
