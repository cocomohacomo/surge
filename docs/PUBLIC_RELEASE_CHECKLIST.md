# Public Release Readiness Checklist (SURGE)

SURGE-specific completed record. For reuse on other repositories, see
[`docs/public-release-template/`](public-release-template/README.md).

This checklist does not itself change repository visibility.

## Owner-selected publication policy

- Repository visibility: **Public** (changed by owner after audit)
- License: MIT. The root `LICENSE` file is authoritative.
- Branches: retain all branches. Do not delete or silently retarget existing branches or pull requests.

## Completed preparation

- [x] Publish the MIT license in the root `LICENSE` and align README and `docs/PUBLICATION_POLICY.md`.
- [x] Add `SECURITY.md` with private vulnerability-disclosure guidance.
- [x] Add ignore rules for local builds, caches, credentials, databases, journals, and logs.
- [x] Harden conformance CI (least privilege, pinned SHAs, concurrency, full test matrix).
- [x] Add Dependabot for Actions and pip.

## Completed audit

- [x] Full-history secret content scan (no findings). See `docs/PUBLIC_RELEASE_AUDIT_REPORT.md`.
- [x] Branch-tip sensitive filename scan (no findings).
- [x] Actions log sample review (no secret patterns in accessible logs).
- [x] Local and post-public CI matrices green.

## Completed owner / GitHub UI

- [x] Default-branch ruleset active (`allow-owner-and-agent-only`):
  deletion block, force-push block, PR required, required checks `cpp-core` + `schema-and-fixtures`
- [x] Secret scanning enabled
- [x] Push protection enabled
- [x] Private vulnerability reporting enabled
- [x] Vulnerability alerts / Dependabot security updates enabled
- [x] Security policy URL live from `SECURITY.md` on `main`

## Reuse

Copy and adapt:

```sh
cp docs/public-release-template/CHECKLIST.md docs/PUBLIC_RELEASE_CHECKLIST.md
python3 tools/verify_public_release_settings.py \
  --owner <owner> --repo <repo> \
  --required-check <job-a> --required-check <job-b>
```
