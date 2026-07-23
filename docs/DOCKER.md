# Docker Hub publishing (one-time setup)

`.github/workflows/docker-publish.yml` builds and pushes all three images (runner,
orchestrator, web) to Docker Hub on every `v*` tag — the automated counterpart to
[`scripts/publish-images.sh`](../scripts/publish-images.sh) (still useful for a local
build/push, or for building the images before they're published so `ebl start` has
something to pull in the meantime).

## 1. Create a Docker Hub access token

Docker Hub → **Account Settings → Security → New Access Token**.

- Description: something identifying this repo (e.g. `expo-builder-local-ci`).
- Access permissions: **Read & Write**.

Copy the token immediately — Docker Hub only shows it once.

## 2. Add the two repository secrets

Repo → **Settings → Secrets and variables → Actions → New repository secret**:

| Secret | Value |
|---|---|
| `DOCKERHUB_USERNAME` | Your Docker Hub username or organization name — this is also the image namespace, e.g. `yourusername/expo-builder-local-runner` |
| `DOCKERHUB_TOKEN` | The access token from step 1 |

If either is missing, the workflow fails fast with a clear `::error::` rather than
silently skipping the push.

## What gets published

Three images, tagged per the pushed git tag (`v0.5.0` → `0.5.0`, `0.5`, and `latest`):

- `<namespace>/expo-builder-local-runner`
- `<namespace>/expo-builder-local-orchestrator`
- `<namespace>/expo-builder-local-web`

All `linux/amd64` only (no cross-platform emulation — the runner image's Android
SDK/NDK download would be slow and untested under QEMU arm64; a reasonable follow-up
if arm64 support is ever needed, not part of this setup).

Once published, point `ebl config`'s Docker Hub namespace (or `DOCKERHUB_NAMESPACE`
in `.env`) at your namespace and `ebl setup`/`ebl start`/`docker-compose.yml` will
pull these instead of needing a local build.
