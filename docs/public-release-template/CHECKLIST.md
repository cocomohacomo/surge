# Public release checklist (blank)

Copy to `docs/PUBLIC_RELEASE_CHECKLIST.md` and fill per repository.
This file does not change repository visibility.

## Policy

- Owner: _______________
- Repository: _______________
- License decision: _______________
- Keep all existing branches/PRs: [ ] yes
- Required CI checks: _______________ / _______________

## Preparation (Private)

- [ ] Publication policy documented
- [ ] `SECURITY.md` present on default branch
- [ ] Secret-oriented `.gitignore` entries
- [ ] Dependabot configured
- [ ] CI least privilege + pinned Actions SHAs + real test matrix
- [ ] Preparation PR merged

## Audit (Private)

- [ ] Full-history / all-refs secret content scan
- [ ] Branch-tip sensitive filename scan
- [ ] Actions log/artifact sample review
- [ ] Local or CI test matrix green
- [ ] Audit report written; no unresolved secrets

## Owner UI before/at Public

- [ ] Vulnerability alerts / Dependabot alerts enabled
- [ ] Default-branch ruleset created or restored after Public
- [ ] Owner explicitly changes visibility to Public

## After Public

- [ ] Secret scanning enabled
- [ ] Push protection enabled
- [ ] Private vulnerability reporting enabled
- [ ] Required status checks match real job names only
- [ ] No phantom checks (typos / missing CodeQL workflow)
- [ ] `tools/verify_public_release_settings.py` exits 0
- [ ] Post-public CI run allocates a runner and passes
