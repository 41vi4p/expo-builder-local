# expo-builder-local

Build a managed Expo (SDK 56+) project into an Android APK/AAB entirely on your own
machine — from the command line or a web GUI — in a disposable Docker container, and
get a properly-signed artifact exported straight into that project's `ebl_builds/`
folder, versioned per build.

No Expo account is required for the default path — everything runs in a disposable
Docker container with its own Android SDK, Node and Gradle. If you'd rather use EAS's
remote-managed credentials, that's supported too (see [Build engines](#build-engines)).

## Quick start (CLI)

```bash
# Install (pick one)
curl -fsSL https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/install.sh | sh
# or add the APT repo yourself (this is what the script above does):
#   curl -fsSL https://41vi4p.github.io/expo-builder-local/apt/pubkey.gpg | sudo gpg --dearmor -o /usr/share/keyrings/ebl-archive-keyring.gpg
#   echo "deb [signed-by=/usr/share/keyrings/ebl-archive-keyring.gpg] https://41vi4p.github.io/expo-builder-local/apt stable main" | sudo tee /etc/apt/sources.list.d/ebl.list
#   sudo apt update && sudo apt install ebl
# or: download ebl_*_amd64.deb from Releases, then: sudo apt install ./ebl_*_amd64.deb

ebl setup     # checks/installs Docker, pulls the runner/orchestrator/web images
ebl config    # interactive: your projects folder, Expo token, ports
ebl start     # runs the orchestrator + web GUI as containers, prints the GUI link

cd /path/to/your/expo/app
ebl build .              # debug-signed APK, auto engine
ebl build . --prod       # shortcut for --artifact aab --profile production
```

`ebl build` never needs `setup`/`config`/`start` — it works standalone, from anywhere,
against any Expo project, talking to Docker directly. `setup`/`config`/`start` are
only for the optional web GUI (live dashboard, build history, keystore manager).

## Why this exists

Expo's managed workflow normally means either `eas build` (cloud, costs money/quota,
needs an Expo account) or manually running `expo prebuild` + Gradle yourself every
time. This tool wraps the second path in a disposable container — driven by a CLI, a
GUI, or both — so any developer can produce a build without setting up an Android SDK
locally or learning Gradle.

## Architecture

```
                 ┌─ ebl build .  (direct, no services needed) ───────────┐
                 │                                                       │
Browser ──HTTP/WS──▶ web (Next.js) ──HTTP/WS──▶ orchestrator (Fastify) ──┴─▶ runner container
   ▲                started by `ebl start`      │ /var/run/docker.sock      (Node + JDK 17 +
   └── ebl setup/config/start drive Docker      ▼                           Android SDK)
       directly — no docker-compose.yml    bind-mounts your project,
       or git checkout required            writes the APK/AAB into
                                            <project>/ebl_builds/
```

- **`cli/`** — the `ebl` command (C++17, CMake): talks to the Docker Engine API
  directly over its unix socket. `ebl build` needs nothing else running; `ebl start`
  launches the orchestrator + web images itself.
- **`expo-builder-gui/`** — the web UI (Next.js): directory browser, build config
  form, live dashboard (progress, logs, CPU/mem/net/disk charts), metrics, history.
- **`orchestrator/`** — a small backend service that spawns and supervises build
  containers via the Docker API, streams their output/stats over WebSocket, and
  persists build history to SQLite.
- **`docker/runner/`** — the Android toolchain image. Not a long-running service: a
  fresh, disposable container is started from it for every single build.

The orchestrator talks to the **host's** Docker daemon over the mounted socket (it is
a sibling container, not a nested one) — see [Path handling](#path-handling-important)
for why that matters.

## Command reference

| Command | What it does |
|---|---|
| `ebl setup` | One-time: checks Docker is installed and running (offers to install it via the official convenience script if not — asks first, needs sudo), then pulls the runner/orchestrator/web images. |
| `ebl config` | Interactive wizard: projects folder (for the GUI's directory browser), Expo access token, orchestrator/web ports, Docker Hub namespace. Saved to `~/.config/ebl/config.json`; secrets encrypted at rest (see [Security notes](#security-notes)). Re-run any time to change a value. |
| `ebl start` | Runs the orchestrator + web GUI as Docker containers (pulling images if needed), waits for both to report healthy, prints the GUI URL. No docker-compose.yml or git checkout needed. |
| `ebl stop` | Stops and removes those two containers. Build history/keystores live in a separate volume and are preserved. |
| `ebl build [path] [options]` | Builds an Expo project. Works completely standalone — see below. |

Run `ebl <command> --help` for the full option list of any command.

### `ebl build`

```bash
ebl build .                    # apk, profile "preview" (or the project's first eas.json profile), engine auto
ebl build . --prod             # --artifact aab --profile production
ebl build . --engine eas --expo-token "$EXPO_TOKEN"
ebl build . --release --keystore ./release.jks --key-alias upload \
  --store-password "$STORE_PW" --key-password "$KEY_PW"
```

Prefer `EXPO_BUILDER_STORE_PASSWORD` / `EXPO_BUILDER_KEY_PASSWORD` / `EXPO_TOKEN`
environment variables over the `--store-password` etc. flags where you can — flag
values are more likely to end up in your shell history. If `ebl config` has already
saved an Expo token or a Docker Hub namespace, `ebl build` picks those up as defaults
too (any explicit flag/env var still wins).

Every successful build lands in `<project>/ebl_builds/v<app-version>-build<n>/` — `n`
is a simple counter local to that project (see `ebl_builds/.build-counter`), so
"build 4" always means the same thing regardless of how many times a given app
version gets rebuilt. `ebl_builds/` is added to the project's `.gitignore`
automatically on first build.

## Using the GUI

Once `ebl start` is running (or after `make up` — see
[Local development](#local-development-contributing-to-this-repo)):

1. **Pick a project.** The directory browser starts at the projects folder you set
   in `ebl config`; navigate into your Expo app's folder. A green "Expo project
   detected" badge means it found a `package.json` with an `expo` dependency — click
   **Use this folder**.
2. **Configure the build.**
   - **Artifact**: APK (installs directly on a device) or AAB (Play Store bundle).
   - **Profile**: pulled from the project's `eas.json` build profiles if present
     (typically `preview`/`production`), otherwise free text.
   - **Engine**: see [Build engines](#build-engines) below.
   - **Signing**: Debug (fast, installable, not for the Play Store) or Release (upload
     a keystore — see [Signing](#signing)).
3. **Watch it build.** The phase rail shows setup → install → prebuild/EAS → signing →
   compile → collect, each with elapsed time, a live percentage + ETA, streamed logs,
   and CPU/memory/network/disk charts for the build container.
4. **Get your artifact.** On success, the metrics panel shows the build number, size
   (with the delta vs your last build of this app+profile), build time, version,
   application ID, a SHA-256, and the exact path under `ebl_builds/` — plus a download
   button.

Note: builds started from the CLI and from the GUI are intentionally independent —
CLI builds aren't recorded in the GUI's history. Use the GUI when you want the live
dashboard and a persistent history; use the CLI for quick one-offs or CI.

## Build engines

| Engine | How | Needs an Expo account? |
|---|---|---|
| **Gradle (local)** | `expo prebuild` generates the native `android/` project, then Gradle compiles it directly in the container. | No — fully offline once dependencies are cached. |
| **EAS (local)** | `eas build --local` — same command EAS's own cloud workers run, just on your machine. Uses your project's `eas.json` profile as-is. | Yes — needs an [Expo access token](https://expo.dev/accounts/[account]/settings/access-tokens) (set via `ebl config`, `EXPO_TOKEN`, or per-build). |
| **Auto** | Uses EAS if a token is available, otherwise falls back to Gradle. | Optional. |

## Signing

- **Debug** — every build is signed with Expo's default debug keystore. Good for
  installing on a test device, not accepted by the Play Store.
- **Release** — provide a real keystore (`.jks`/`.keystore`) — via `--keystore` on the
  CLI, or uploaded once in the GUI's keystore manager and selected per build. The
  password/alias/key-password are AES-256-GCM encrypted at rest (GUI: server-side;
  CLI: n/a, passed directly per invocation) and only decrypted in memory for the one
  build that uses them.
  - Gradle engine: the keystore is wired into a generated `keystore.properties` and a
    patched `android/app/build.gradle` `release` signing config, both removed again
    the moment the build finishes (success or failure) — they never persist in your
    project folder.
  - EAS engine: written to a temporary local `credentials.json` (EAS's own local-build
    format) and an `eas.json` `credentialsSource: "local"` override for that profile,
    also cleaned up automatically after the build.

## Docker Hub images

Three images, all under one namespace (default placeholder `ebllocal` — set your own
via `ebl config` or the `DOCKERHUB_NAMESPACE` env var before publishing):

- `<namespace>/expo-builder-local-runner` — the Android toolchain.
- `<namespace>/expo-builder-local-orchestrator` — the backend.
- `<namespace>/expo-builder-local-web` — the GUI (runtime-configurable: the
  orchestrator URL is substituted into the compiled bundle at container *start*, from
  the `ORCHESTRATOR_URL` env var — not baked in at build time, so one published image
  works regardless of what port a given user picks).

Build (and optionally push) all three:

```bash
DOCKERHUB_NAMESPACE=yourusername ./scripts/publish-images.sh          # build only
DOCKERHUB_NAMESPACE=yourusername ./scripts/publish-images.sh --push   # build + push (needs `docker login` first)
```

`ebl build`/`ebl setup`/`ebl start` all prefer pulling from the configured namespace,
falling back to a local build (for the runner image only — orchestrator/web have no
local-build fallback, since they're meant to be pre-published; build them from this
repo via the script above if you need them before they're published).

## APT repository

Every `v*` tag publishes a real, GPG-signed APT repository to GitHub Pages at
`https://41vi4p.github.io/expo-builder-local/apt` (see [Quick start](#quick-start-cli)
for the add-the-repo commands, or just use `install.sh`, which does it for you). Once
added, `sudo apt upgrade` picks up new `ebl` releases automatically — this is the
recommended install path over downloading a `.deb` by hand.

See [`docs/APT_REPO_SETUP_GUIDE.md`](./docs/APT_REPO_SETUP_GUIDE.md) for the one-time
signing-key setup this depends on, and [`docs/RELEASING.md`](./docs/RELEASING.md) for
the release process itself (GitHub Actions, tag-triggered).

## Local development (contributing to this repo)

If you're working on the orchestrator/GUI themselves, `docker-compose.yml` is still
the quickest loop (rebuilds on `docker compose up -d --build`, no need to reinstall
the CLI or images each time):

```bash
cd expo-builder-local
cp .env.example .env   # set HOST_PROJECTS_ROOT, MASTER_KEY (openssl rand -base64 32), HOST_UID/HOST_GID
make build-image        # builds the Android toolchain image — large, one-time (~10-20 min)
make up                 # builds + starts the GUI and orchestrator
```

Open the GUI at `http://localhost:3000`. See [Path handling](#path-handling-important)
for why `HOST_PROJECTS_ROOT` has to be an absolute path that matches on both sides of
the bind mount.

```bash
cd orchestrator && npm install && npm run dev      # Fastify + tsx watch, port 4001
cd expo-builder-gui && npm install && npm run dev  # Next.js dev server, port 3000
```

Building the CLI for local iteration:

```bash
make install-cli   # builds (CMake/C++) and installs to ~/.local/bin — see Makefile for CLI_BUILD_DIR
make deb           # builds a .deb locally (unsigned unless you've set up dpkg-sig yourself)
```

## Path handling (important)

The orchestrator container does **not** have its own copy of your projects — it talks
to the **host** Docker daemon over `/var/run/docker.sock` and tells it to bind-mount
your project folder into a *new sibling container*. Because the daemon resolves those
paths against the real host filesystem, your projects folder must be bind-mounted into
the orchestrator at the **exact same path** it has on the host — both `ebl start` and
`docker-compose.yml` already do this for you. If you ever see "file not found" errors
referencing a path that looks right, double-check the configured projects folder is
an absolute, real host path.

## Security notes

- Both services bind to `127.0.0.1` by default. The orchestrator's access to the
  Docker socket is root-equivalent on your host — don't expose its port beyond
  localhost without understanding that.
- The apps this tool was built against (and likely yours too) commit real secrets to
  `.env`/`eas.json`/`google-services.json` — the orchestrator redacts every value it
  can find in those files from streamed and persisted build logs, but that's a safety
  net, not a fix. Rotate any secret that was already public.
- `ebl config`'s saved settings live at `~/.config/ebl/config.json` (0600) with the
  Expo token and the orchestrator's generated `MASTER_KEY` AES-256-GCM-encrypted using
  a machine-local key at `~/.config/ebl/machine.key` (0600, generated on first use,
  never leaves the machine).
- Keystore passwords (GUI upload path) are encrypted at rest; the keystore file itself
  is stored as plain bytes (Gradle/EAS both need a real file path) under the
  `expo-builder-data` Docker volume.
- Every uploaded keystore, and this tool's own generated signing config, stays inside
  Docker-managed storage or is deleted at the end of a build — nothing sensitive is
  left sitting in your project folder afterward.

## Troubleshooting

- **`ebl setup` says Docker isn't reachable after installing it** — you likely need to
  log out and back in (or run `newgrp docker`) so your user session picks up
  docker-group membership, then re-run `ebl setup`.
- **`ebl start` fails to pull the orchestrator/web image** — they're not published to
  the configured namespace yet. Build them locally first:
  `./scripts/publish-images.sh` (no `--push` needed for local-only use).
- **`install.sh` falls back to a direct download instead of using apt** — the hosted
  APT repo isn't live yet (no tag has been pushed, or GitHub Pages isn't enabled — see
  `docs/APT_REPO_SETUP_GUIDE.md`), or you're not on a Debian/Ubuntu-family system.
- **`apt install ebl` fails with a signature/NO_PUBKEY error** — the keyring at
  `/usr/share/keyrings/ebl-archive-keyring.gpg` is missing or stale; re-run the three
  `curl`/`gpg`/`tee` commands from [Quick start](#quick-start-cli), or just re-run
  `install.sh`.
- **"Path is outside the configured allowed roots"** (GUI) — the projects folder set
  via `ebl config`/`HOST_PROJECTS_ROOT` doesn't cover the folder you picked, or (for
  docker-compose) the bind mount wasn't rebuilt after changing it.
- **Build hangs at "Install"** — first build for a project downloads its full
  `node_modules`; subsequent builds reuse the shared `npm-cache`/`gradle-cache`
  volumes and are much faster.
- **"eas build --local failed" / credential errors** — the EAS engine needs a real
  Expo access token (via `ebl config`, `EXPO_TOKEN`, or `--expo-token`); it also
  expects the project's `eas.json` profile to be otherwise valid.
- **AAB isn't accepted by the Play Store** — make sure you built with **Release**
  signing (`--release`/GUI Release) and a real upload keystore, not the debug default.
