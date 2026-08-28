# Security Policy

## Reporting security issues

Do not publish credentials, private keys, access tokens, personal dumps or sensitive device/account data in a public issue.

For ordinary crashes and hardware bugs, sanitize logs before attaching them publicly.

## Repository hygiene

Never commit:

- GitHub tokens or PATs;
- `.env` files containing secrets;
- private SSH/GPG keys;
- Wi-Fi passwords;
- personal account identifiers;
- private debug archives containing unrelated user data.

If a secret is committed, removing the file in a later commit is not sufficient. Revoke/rotate the secret and remove it from Git history before publishing.
