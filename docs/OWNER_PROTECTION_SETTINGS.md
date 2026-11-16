# Owner protection settings (SURGE)

SURGE-specific notes. The reusable instructions live in
[`docs/public-release-template/OWNER_SETTINGS.md`](public-release-template/OWNER_SETTINGS.md)
and [`docs/public-release-template/RULESET.md`](public-release-template/RULESET.md).

## Current SURGE status

- Visibility: Public
- Advanced Security page: https://github.com/cocomohacomo/surge/settings/security_analysis
- Ruleset: https://github.com/cocomohacomo/surge/rules/27364131
- Secret scanning: enabled
- Push protection: enabled
- Private vulnerability reporting: enabled
- Required checks: `cpp-core`, `schema-and-fixtures`

## Verify

```sh
python3 tools/verify_public_release_settings.py \
  --owner cocomohacomo --repo surge \
  --required-check cpp-core \
  --required-check schema-and-fixtures
```
