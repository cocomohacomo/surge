# Public release playbook

Ordered procedure. Visibility change is owner-only and comes last among the
pre-public gates.

## 0. Policy decisions (owner)

Record these before editing files:

| Decision | SURGE choice | Notes |
| --- | --- | --- |
| License | none (all rights reserved) | Public ≠ licensed |
| Keep existing branches/PRs | yes | Do not delete or silently retarget |
| Required CI check names | `cpp-core`, `schema-and-fixtures` | Must match workflow job names exactly |
| Ruleset name | `allow-owner-and-agent-only` | Optional; any stable name is fine |

## 1. Preparation PR (while Private)

- [ ] Add publication policy (`docs/PUBLICATION_POLICY.md` or template file)
- [ ] Add `SECURITY.md`
- [ ] Expand `.gitignore` for secrets and local artifacts (see `files/gitignore.secrets.snippet`)
- [ ] Add Dependabot (see `files/dependabot.yml.example`)
- [ ] Harden CI:
  - `permissions: contents: read`
  - pin Actions to full commit SHAs
  - concurrency cancel-in-progress
  - run the real test matrix
- [ ] Link checklist from README
- [ ] Merge preparation PR to default branch

## 2. Audit (while Private)

- [ ] Content secret scan across **all refs / reachable blobs**
- [ ] Sensitive filename scan on every remote branch tip
- [ ] Sample Actions logs/artifacts for leaked tokens
- [ ] Local or CI green for the intended public test matrix
- [ ] Write an audit report (findings + residual owner actions)
- [ ] If any secret is found: revoke/rotate first, then clean history

Do **not** flip visibility with unresolved secrets.

## 3. Pre-public owner UI (may be limited on free Private)

On free private personal repos, rulesets / some protections may require
**GitHub Pro** or waiting until Public. Prefer enabling what is available, then
re-check after Public.

- [ ] Vulnerability / Dependabot alerts on
- [ ] Draft default-branch ruleset (see RULESET.md) if the UI allows

## 4. Visibility change (owner only)

1. Confirm audit has no open secret findings
2. Merge remaining preparation/audit PRs
3. Settings → General → Danger Zone → **Change repository visibility → Public**
4. Notify the verifier / re-run `tools/verify_public_release_settings.py`

Agents and CI tokens must not automate this step.

## 5. Immediately after Public

Open:

`https://github.com/<owner>/<repo>/settings/security_analysis`

Enable:

- [ ] Secret scanning / Secret Protection
- [ ] Push protection
- [ ] Private vulnerability reporting

Open ruleset:

`https://github.com/<owner>/<repo>/rules`

Ensure default-branch ruleset includes:

- [ ] Block creations that bypass policy (as desired)
- [ ] Block updates / force-push (`non_fast_forward`) / deletion
- [ ] Require pull request (approval count as desired)
- [ ] Required status checks = exact CI job names only  
      (do **not** add typo contexts or CodeQL unless that workflow exists)

## 6. Verify

```sh
python3 tools/verify_public_release_settings.py \
  --owner <owner> --repo <repo> \
  --required-check cpp-core \
  --required-check schema-and-fixtures
```

Optional CI smoke: push an empty commit on a side branch and confirm runners
allocate (`runner_id != 0`) and jobs succeed. Private-minute exhaustion often
clears after Public.
