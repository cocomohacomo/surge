# Owner protection settings (manual)

This agent cannot change repository administration settings with the current
GitHub token (`Resource not accessible by integration`). Apply these settings
in the GitHub UI before or immediately after making the repository public.

## Before Public

1. **Settings → Code security**
   - Enable **Private vulnerability reporting** (already referenced by
     `SECURITY.md`).
   - After the repository is Public, enable **Secret scanning** and
     **Push protection**. On public repositories these features are available
     without GitHub Advanced Security.
2. **Settings → Branches → Add branch protection rule** for `main`
   - Require a pull request before merging
   - Require status checks: `cpp-core`, `schema-and-fixtures`
   - Block force pushes
   - Do not allow deletions
3. **Settings → Actions → General**
   - Confirm Actions are enabled
   - Prefer read-limited default workflow permissions
4. **Settings → Webhooks / Deploy keys**
   - Remove any unused webhook or deploy key before publication

## Visibility change

Only the repository owner should change visibility:

1. Merge the public-release preparation pull request(s).
2. Confirm the audit report has no open secret findings.
3. **Settings → General → Danger Zone → Change repository visibility → Public**.

Do not automate this step from CI or agent tokens.

## After Public

1. Confirm Secret scanning and Push protection are Enabled.
2. Confirm Dependabot alerts are available.
3. Re-run the conformance workflow; public repositories receive separate
   Actions minute accounting and may succeed where private-runner allocation
   previously failed with `runner_id: 0`.
