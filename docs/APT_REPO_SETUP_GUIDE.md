# APT repository setup (one-time)

`.github/workflows/release.yml` publishes a real, signed APT repository to GitHub
Pages on every `v*` tag — so `apt install ebl` works after adding the repo, with
`apt upgrade` picking up new releases automatically. This is what makes that
possible; it's a one-time setup, not something the workflow does for you (on
purpose — it needs a private key, which nothing automated should generate for you).

Unlike `dpkg-sig` signing an individual `.deb` (verifiable, but not something
`apt`/`dpkg` check automatically), this signs the repository's `Release` file the
way any real APT repo does — `apt` verifies it **before every install**, and refuses
untrusted packages outright once the key is added.

## 1. Generate a signing key

```bash
gpg --full-generate-key
```

- Key type: RSA and RSA (default), 4096 bits — or ed25519 if your `gpg` supports it.
- Expiration: your call. A key with no expiry is simplest to operate; 1-2 years with
  a calendar reminder to rotate is a reasonable middle ground.
- **Passphrase: leave it empty.** The private key material already lives as an
  encrypted GitHub secret — an additional passphrase mainly protects a key sitting on
  a workstation's disk, and skipping it avoids fragile non-interactive-passphrase
  plumbing in CI (`gpg --batch --import` doesn't prompt, but signing steps would need
  `--pinentry-mode loopback` wired through correctly if the key *were* passphrase
  protected, which is more moving parts than this needs).

Find the key's ID:

```bash
gpg --list-secret-keys --keyid-format=long
# sec   rsa4096/ABCD1234EFGH5678 2026-07-23 [SC]
#       ← the part after the slash is the key ID
```

## 2. Export both halves

```bash
# Private — goes into a GitHub secret, never committed.
gpg --armor --export-secret-keys ABCD1234EFGH5678 > ebl-release-signing-key.asc

# Public — gets committed to the repo as-is, so `apt` (and anyone downloading the
# package manually) can verify what CI signs.
gpg --armor --export ABCD1234EFGH5678 > docs/apt/pubkey.gpg
```

Commit `docs/apt/pubkey.gpg`. Do **not** commit `ebl-release-signing-key.asc` — it's
already gitignored; delete your local copy once it's safely in GitHub Secrets (step 3)
unless you keep it elsewhere on purpose.

## 3. Add the private key as a repository secret

Repo → **Settings → Secrets and variables → Actions → New repository secret**:

| Secret | Value |
|---|---|
| `GPG_PRIVATE_KEY` | The full contents of `ebl-release-signing-key.asc` (including the `-----BEGIN/END PGP PRIVATE KEY BLOCK-----` lines) |

If this secret is missing, the workflow's signing step fails loudly (`::error::`)
rather than silently publishing an unsigned repo.

## 4. Enable GitHub Pages

Repo → **Settings → Pages** → **Source: Deploy from a branch** → **Branch:
`gh-pages`** (the workflow creates this branch itself on first successful run via
`peaceiris/actions-gh-pages`, so it won't exist until *after* the first tag push —
you can come back and flip this setting right after that first run completes, or set
it in advance and let the first publish populate it).

Once live, the repo is served at:

```
https://41vi4p.github.io/expo-builder-local/apt
```

## What end users do

```bash
curl -fsSL https://41vi4p.github.io/expo-builder-local/apt/pubkey.gpg \
  | sudo gpg --dearmor -o /usr/share/keyrings/ebl-archive-keyring.gpg

echo "deb [signed-by=/usr/share/keyrings/ebl-archive-keyring.gpg] https://41vi4p.github.io/expo-builder-local/apt stable main" \
  | sudo tee /etc/apt/sources.list.d/ebl.list

sudo apt update
sudo apt install ebl
```

`install.sh` does exactly this automatically when the hosted repo is reachable,
falling back to a direct `.deb`/tarball download otherwise (see
[`../README.md`](../README.md)).

## Cutting a release

Same as before — this doesn't change with the APT repo setup:

```bash
git tag v0.3.1
git push origin v0.3.1
```

Bump `orchestrator/package.json`, `expo-builder-gui/package.json`, and
`cli/CMakeLists.txt`'s `project(... VERSION ...)` to match *before* tagging — see
`../CLAUDE.md`'s version-management section.
