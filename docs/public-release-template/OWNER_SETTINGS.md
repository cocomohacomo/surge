# Owner GitHub Settings (UI)

Replace `<owner>` / `<repo>` with the target repository.

## Security analysis

Direct link:

`https://github.com/<owner>/<repo>/settings/security_analysis`

Path: repository **Settings** → **Security and quality** / **Security** →
**Advanced Security**

Enable:

1. **Secret Protection** / **Secret scanning**
2. **Push protection** (under Secret Protection)
3. **Private vulnerability reporting**
4. Confirm Dependabot alerts / security updates as desired

## Rulesets (preferred over classic branch protection)

Direct link:

`https://github.com/<owner>/<repo>/rules`

Path: **Settings** → **Rules** → **Rulesets**

Apply the recipe in [RULESET.md](RULESET.md) to the default branch
(`~DEFAULT_BRANCH` or `refs/heads/main`).

## Visibility

Path: **Settings** → **General** → **Danger Zone** →
**Change repository visibility** → **Public**

Only the repository owner should perform this action after the audit is clean.

## Common UI confusion

| Looking for | Where it actually is |
| --- | --- |
| Secret scanning | Settings → Advanced Security / security_analysis |
| Push protection | Same page, under Secret Protection |
| Private vulnerability reporting | Same page |
| Required CI checks | Settings → Rules → Rulesets → edit ruleset |
| Classic branch protection | Settings → Branches (optional; rulesets preferred) |

If a left-nav item is missing on free Private repos, finish the audit, switch to
Public, then return to the same links.
