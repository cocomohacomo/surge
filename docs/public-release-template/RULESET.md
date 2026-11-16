# Default-branch ruleset recipe

Proven shape from the SURGE public conversion. Create under
**Settings → Rules → Rulesets**.

## Target

- Enforcement: **Active**
- Target: **Branch**
- Include: `~DEFAULT_BRANCH` (or `refs/heads/main`)

## Rules to include

1. **Restrict creations** (optional but used by SURGE)
2. **Restrict updates** (block direct pushes to default branch)
3. **Block force pushes** (`non_fast_forward`)
4. **Block deletions**
5. **Require a pull request before merging**
   - Required approvals: `1` (adjust if solo-maintainer bypass is desired)
   - Allowed merge methods: merge / squash / rebase as preferred
6. **Require status checks to pass**
   - Add **only** real workflow job names, e.g.:
     - `cpp-core`
     - `schema-and-fixtures`
   - Do **not** add misspelled contexts
   - Do **not** require CodeQL / other tools unless that workflow exists

## Rules to avoid unless intentionally supported

- Require CodeQL / code scanning tool results without a CodeQL workflow
- Extra status-check contexts that never appear on PRs
- Over-broad required reviewers that block the sole maintainer with no bypass

## Bypass

Owner/admin bypass may remain available for emergencies. Prefer fixing failing
required checks over routine bypass.
