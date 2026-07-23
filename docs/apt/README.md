# docs/apt/

This directory holds exactly one file once you've completed
[`../APT_REPO_SETUP_GUIDE.md`](../APT_REPO_SETUP_GUIDE.md): **`pubkey.gpg`** — the
*public* half of the GPG key that signs the APT repository. It's meant to be
committed (public keys aren't secret; the private half lives only in the
`APT_GPG_PRIVATE_KEY` GitHub Actions secret).

Until `pubkey.gpg` exists here, `.github/workflows/release.yml` fails fast on tag
push with a message pointing back to the setup guide, rather than silently
publishing an unsigned or inconsistently-signed repo.
