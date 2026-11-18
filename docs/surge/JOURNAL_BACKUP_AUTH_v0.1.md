# Authenticated EventJournal Backups v0.1

Status: optional HMAC-SHA256 manifest authentication.

## Threat model

The backup manifest already records the journal's byte size, SHA-256 digest,
and replay summary. Those fields detect accidental corruption, but an attacker
who can replace both the journal and its adjacent manifest can recompute them.

Optional HMAC-SHA256 authentication binds the manifest metadata and journal
SHA-256 digest to a secret key held outside the repository. The key is never
copied into the backup or printed by the CLI. Verification uses constant-time
signature comparison.

HMAC provides authenticity and integrity, not confidentiality. Encrypt backup
files separately when they contain sensitive data. Key storage, access
control, rotation, escrow, and recovery remain the operator's responsibility.

## Key requirements

- Use at least 32 bytes of cryptographically random key material.
- Keep the key outside the repository and outside the backup directory.
- Restrict file access to the backup service/operator (for example, POSIX mode
  \`0600\` or a managed secret store materialized to a protected file).
- \`key_id\` is a non-secret identifier. It is recorded in the manifest.
- Retain old keys while any backup signed by those keys must remain verifiable.
  Rotation means creating a new key and key ID; it does not rewrite old backups.

Example key creation on a trusted POSIX host:

\`\`\`sh
umask 077
openssl rand -out /secure/surge-backup-v1.key 32
\`\`\`

Do not commit the key or include it in support bundles.

## CLI

Create a signed backup:

\`\`\`sh
python tools/journal_backup.py backup live.sgj backups/live.sgj \
  --key-file /secure/surge-backup-v1.key --key-id ops-2026-v1
\`\`\`

Verify and require authentication:

\`\`\`sh
python tools/journal_backup.py verify backups/live.sgj \
  --key-file /secure/surge-backup-v1.key --key-id ops-2026-v1 --require-auth
\`\`\`

Restore and require authentication:

\`\`\`sh
python tools/journal_backup.py restore backups/live.sgj restored.sgj \
  --key-file /secure/surge-backup-v1.key --key-id ops-2026-v1 \
  --require-auth
\`\`\`

The Python API accepts the same \`key_file\`, \`key_id\`, and \`require_auth\`
arguments. Unsigned legacy backups remain readable by default for migration,
but \`require_auth=True\` / \`--require-auth\` rejects them. Supplying an auth key
to verify an unsigned backup also fails closed.

## Signed fields and verification

The HMAC-SHA256 is computed over canonical UTF-8 JSON containing every
manifest field except the \`auth\` object itself. The auth object contains only:

- \`algorithm\`: \`HMAC-SHA256\`
- \`key_id\`: operator-managed key identifier
- \`hmac_sha256\`: lowercase hexadecimal signature

Verification first checks the backup digest and replay summary, then validates
the auth metadata, key identifier (when supplied), key length, and HMAC. A
missing key, wrong key ID, wrong key, altered metadata, or unsigned backup
when authentication is required is rejected before restore acquires the
destination writer lock or writes destination bytes.

## Limitations

- HMAC does not encrypt the journal.
- A compromised signing key permits forged manifests and backups.
- There is no automatic KMS/HSM integration or key rotation service yet.
- The adjacent manifest and backup must both be retained. Off-host replication
  and immutable retention are separate operational controls.
- HMAC authentication does not replace native C++ replay validation after
  restore.
