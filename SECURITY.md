# Security Policy

## Reporting a vulnerability

Please do not report suspected security vulnerabilities in public issues, pull
requests, discussions, or commit messages.

If GitHub's private vulnerability reporting feature is enabled for this
repository, use the repository's **Report a vulnerability** function to send
the report privately.

If that function is unavailable, contact the repository owner through the
GitHub profile at https://github.com/cocomohacomo and ask for a private
disclosure channel. Do not include exploit details in a public message.

Please include, where possible:
- affected commit, branch, file, or version;
- impact and conditions required to reproduce the issue;
- a minimal reproduction or proof of concept;
- any suggested mitigation.

There is currently no published response-time or remediation SLA. Please allow
the maintainer reasonable time to investigate before public disclosure.

## Repository hardening notes

- Local secrets must stay out of Git. See `.gitignore` for `.env`, key material,
  and credential filename patterns.
- Dependabot is configured in `.github/dependabot.yml` for GitHub Actions and
  pip dependency update PRs.
- Branch protection, Secret scanning, and Push protection must be enabled by
  the repository owner in GitHub Settings. See
  `docs/OWNER_PROTECTION_SETTINGS.md`.
