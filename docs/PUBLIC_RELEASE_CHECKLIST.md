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

## Audit observations

- The initial inventory contained 36 branches. The preparation branch adds one more; all 36 original branches remain, and the repository currently has 37 branches.
- Branch-tip tree filename checks found no obvious `.env`, private-key, credential, database, or log files. This is a filename inventory, not a content-level secret scan.
- The latest successful pre-preparation workflow run completed its two existing jobs successfully and produced no artifacts. A sample of its job logs did not reveal common token/private-key patterns.
- New preparation-branch runs have failed before any job step started: both jobs report zero steps and runner ID 0. The runner was not allocated, so the build and tests were not executed. The cause is not established from the available run metadata.
- The repository has no configured main-branch protection in the last inspected state.
- GitHub code search returned incomplete results; it is not sufficient evidence that the repository contains no secrets.
- A complete scan of every blob in every branch and all historical Git objects has not been performed.

## Required before changing visibility

- [ ] Run a content-based secret scan across every branch and the complete Git history. If a secret is found, revoke/rotate it first, then remove it from all exposed history where appropriate.
- [ ] Review all historical Actions logs and artifacts for accidental disclosure. The sampled successful run had no artifacts; this is not a complete review of all 384 recorded runs.
- [ ] Resolve why the new workflow jobs are not receiving runners, then obtain a successful run of the updated C++ and Python test matrix.
- [ ] Configure main-branch protection: require pull requests, require the conformance checks, block force pushes, and prevent branch deletion.
- [ ] Review all 37 branch tips and all open pull-request diffs for unpublished work, personal data, confidential third-party material, or accidental credentials.
- [ ] Review repository settings for Actions permissions, fork policy, vulnerability reporting, and any webhooks or deploy keys.
- [ ] Have the owner review the final public diff and explicitly confirm the visibility change.

Until every required item is reviewed, keep the repository Private. No automated checklist item changes repository visibility.
