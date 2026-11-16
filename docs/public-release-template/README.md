# Public release template

Reusable playbook for converting a **private** GitHub repository to **public**
with the same protection posture used for SURGE.

## What this template covers

1. Pre-public repo hardening (policy docs, `.gitignore`, Dependabot, CI least privilege)
2. Secret / history audit before visibility change
3. Owner-only GitHub Settings (ruleset, secret scanning, push protection, private vulnerability reporting)
4. Post-public verification

## How to use on a new repository

1. Copy the blank checklist:
   ```sh
   cp docs/public-release-template/CHECKLIST.md docs/PUBLIC_RELEASE_CHECKLIST.md
   ```
2. Follow [PLAYBOOK.md](PLAYBOOK.md) in order. Do **not** change visibility until the
   audit section is complete.
3. Apply copyable files from [files/](files/) as needed.
4. Configure GitHub UI using [OWNER_SETTINGS.md](OWNER_SETTINGS.md) and
   [RULESET.md](RULESET.md).
5. After Public, run the verifier:
   ```sh
   python3 tools/verify_public_release_settings.py --owner <owner> --repo <repo> \
     --required-check cpp-core --required-check schema-and-fixtures
   ```

## SURGE instance

The completed SURGE-specific records remain at:

- `docs/PUBLIC_RELEASE_CHECKLIST.md`
- `docs/PUBLIC_RELEASE_AUDIT_REPORT.md`
- `docs/OWNER_PROTECTION_SETTINGS.md`
- `docs/PUBLICATION_POLICY.md`
