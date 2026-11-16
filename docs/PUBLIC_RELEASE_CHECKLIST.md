# Public Release Readiness Checklist

This checklist records the repository preparation state. It does not authorize or
perform a visibility change.

## Owner-selected publication policy

- Repository visibility: keep Private until the owner separately confirms Public.
- License: none. No MIT, Apache-2.0, or other general copyright license is granted.
- Branches: retain all branches. Do not delete or silently retarget existing branches or pull requests.

## Completed in the preparation pull request

- [x] Remove the accidentally proposed MIT License.
- [x] State the no-license policy in README and `docs/PUBLICATION_POLICY.md`.
- [x] Add `SECURITY.md` with private vulnerability-disclosure guidance.
- [x] Add ignore rules for local builds, caches, local databases, journals, and logs.
- [x] Configure Actions to run on pushes to all branches, pull requests targeting any branch, and manual dispatch.
- [x] Restrict workflow token permissions to `contents: read`.
- [x] Pin checkout and setup-python actions to full commit SHAs.
- [x] Add concurrency cancellation.
- [x] Include CTest, schema/fixture validation, and Python adapter regression tests in CI.

## Completed in the audit follow-up

- [x] Run a content-based secret scan across every reachable blob in every ref (508 blobs). Result: no common secret patterns found. Details: `docs/PUBLIC_RELEASE_AUDIT_REPORT.md`.
- [x] Review accessible Actions logs and artifacts for accidental disclosure. Result: 8 logged runs clean; sampled runs had 0 artifacts; most recent failures never allocated a runner.
- [x] Obtain a successful local run of the updated C++ and Python test matrix (CTest 10/10, fixtures OK, unittest 55/55). GitHub-hosted runners still fail to allocate (`runner_id: 0`).
- [x] Review all remote branch tips for sensitive filenames (38 refs): none found.
- [x] Expand `.gitignore` for `.env`, keys, and credential filenames.
- [x] Add Dependabot config for Actions and pip ecosystems.
- [x] Document owner-only GitHub UI protection steps in `docs/OWNER_PROTECTION_SETTINGS.md`.

## Still required (owner / GitHub UI)

- [ ] Configure main-branch protection: require pull requests, require the conformance checks, block force pushes, and prevent branch deletion. *Blocked for agents: administration API returns 403.*
- [ ] Enable private vulnerability reporting, and after Public enable Secret scanning + Push protection. *GitHub Advanced Security / admin APIs unavailable to the agent token while Private.*
- [ ] Review repository settings for Actions permissions, fork policy, webhooks, and deploy keys. *Admin APIs return 403; owner UI review required.*
- [ ] Resolve GitHub-hosted runner allocation (`runner_id: 0`) or re-check after Public (public repos use separate Actions minute accounting).
- [ ] Have the owner review the final public diff and explicitly confirm the visibility change.

Until every owner/UI item above is completed, keep the repository Private. No automated checklist item changes repository visibility.
