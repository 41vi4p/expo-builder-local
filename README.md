# expo-builder-local

Build a managed Expo (SDK 56+) project into an Android APK/AAB entirely on your own
machine, from a web GUI: pick a project folder, click build, watch live logs and
resource usage, and get a properly-signed artifact exported straight into that
project's `build-output/` folder.

No Expo account is required for the default path — everything runs in a disposable
Docker container with its own Android SDK, Node and Gradle. If you'd rather use EAS's
remote-managed credentials, that's supported too (see [Build engines](#build-engines)).

## Why this exists

Expo's managed workflow normally means either `eas build` (cloud, costs money/quota,
needs an Expo account) or manually running `expo prebuild` + Gradle yourself every
time. This tool wraps the second path in a disposable container and a GUI, so any
developer on the team can produce a build without setting up an Android SDK locally
or learning Gradle.

## Architecture

```
Browser ──HTTP/WS──▶ expo-builder-gui (Next.js)
                            │
                            ▼ (browser calls the orchestrator directly)
                      orchestrator (Fastify + dockerode)
                            │ /var/run/docker.sock
                            ▼
                   runner container (Node + JDK 17 + Android SDK)
                     bind-mounts your project folder, builds it,
                     writes the APK/AAB into <project>/build-output/
```

- **`expo-builder-gui/`** — the web UI (Next.js): directory browser, build config
  form, live dashboard (progress, logs, CPU/mem/net/disk charts), metrics, history.
- **`orchestrator/`** — a small backend service that spawns and supervises build
  containers via the Docker API, streams their output/stats over WebSocket, and
  persists build history to SQLite.
- **`docker/runner/`** — the Android toolchain image. Not a long-running service: the
  orchestrator starts a fresh, disposable container from it for every single build.

The orchestrator talks to the **host's** Docker daemon over the mounted socket (it is
a sibling container, not a nested one) — see [Path handling](#path-handling-important)
for why that matters.

## Setup

**Prerequisites:** Docker with Compose v2+, and enough disk (~10GB) for the Android
SDK image. No Android Studio, JDK, or Node install needed on the host itself.

```bash
cd expo-builder-local
cp .env.example .env
```

Edit `.env`:

- `HOST_PROJECTS_ROOT` — the absolute host path to the folder containing your Expo
  project(s) (a common parent folder is fine; you navigate into the actual project in
  the GUI). **This must be an absolute path that exists on your machine.**
- `MASTER_KEY` — generate with `openssl rand -base64 32`. Encrypts uploaded keystore
  passwords at rest.
- `HOST_UID` / `HOST_GID` — run `id -u` / `id -g` and fill these in, so build output
  lands in your project folder owned by you, not root.

Then:

```bash
make build-image   # builds the Android toolchain image — large, one-time (~10-20 min)
make up            # builds + starts the GUI and orchestrator
```

Open the GUI at `http://localhost:3000`.

## Using it

1. **Pick a project.** The directory browser starts at `HOST_PROJECTS_ROOT`; navigate
   into your Expo app's folder. A green "Expo project detected" badge means it found a
   `package.json` with an `expo` dependency — click **Use this folder**.
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
4. **Get your artifact.** On success, the metrics panel shows size (with the delta vs
   your last build of this app+profile), build time, version, application ID, and a
   SHA-256 — plus a download button. The same file is also sitting in
   `<your-project>/build-output/`.

## Build engines

| Engine | How | Needs an Expo account? |
|---|---|---|
| **Gradle (local)** | `expo prebuild` generates the native `android/` project, then Gradle compiles it directly in the container. | No — fully offline once dependencies are cached. |
| **EAS (local)** | `eas build --local` — same command EAS's own cloud workers run, just on your machine. Uses your project's `eas.json` profile as-is. | Yes — needs an [Expo access token](https://expo.dev/accounts/[account]/settings/access-tokens) (`EXPO_TOKEN` in `.env`, or per-build in the GUI). |
| **Auto** | Uses EAS if a token is available (build-time field or `.env` default), otherwise falls back to Gradle. | Optional. |

## Signing

- **Debug** — every build is signed with Expo's default debug keystore. Good for
  installing on a test device, not accepted by the Play Store.
- **Release** — upload a real keystore (`.jks`/`.keystore`) once in the GUI's keystore
  manager; select it per build. The password/alias/key-password are AES-256-GCM
  encrypted at rest and only decrypted in memory for the one build that uses them —
  the API never returns them once saved.
  - Gradle engine: the keystore is wired into a generated `keystore.properties` and a
    patched `android/app/build.gradle` `release` signing config, both removed again
    the moment the build finishes (success or failure) — they never persist in your
    project folder.
  - EAS engine: written to a temporary local `credentials.json` (EAS's own local-build
    format) and an `eas.json` `credentialsSource: "local"` override for that profile,
    also cleaned up automatically after the build.

## Path handling (important)

The orchestrator container does **not** have its own copy of your projects — it talks
to the **host** Docker daemon over `/var/run/docker.sock` and tells it to bind-mount
your project folder into a *new sibling container*. Because the daemon resolves those
paths against the real host filesystem, `HOST_PROJECTS_ROOT` must be bind-mounted into
the orchestrator at the **exact same path** it has on the host — `docker-compose.yml`
already does this for you. If you ever see "file not found" errors referencing a path
that looks right, double check `HOST_PROJECTS_ROOT` is an absolute, real host path.

## Security notes

- Both services bind to `127.0.0.1` by default. The orchestrator's access to the
  Docker socket is root-equivalent on your host — don't expose its port beyond
  localhost without understanding that.
- The apps this tool was built against (and likely yours too) commit real secrets to
  `.env`/`eas.json`/`google-services.json` — the orchestrator redacts every value it
  can find in those files from streamed and persisted build logs, but that's a safety
  net, not a fix. Rotate any secret that was already public.
- Keystore passwords are encrypted at rest; the keystore file itself is stored as
  plain bytes (Gradle/EAS both need a real file path) under the `expo-builder-data`
  Docker volume.
- Every uploaded keystore, and this tool's own generated signing config, stays inside
  Docker-managed storage or is deleted at the end of a build — nothing sensitive is
  left sitting in your project folder afterward.

## Troubleshooting

- **"Path is outside the configured allowed roots"** — `HOST_PROJECTS_ROOT` in `.env`
  doesn't cover the folder you picked, or the compose bind mount wasn't rebuilt after
  changing it (`docker compose up -d --build orchestrator`).
- **Build hangs at "Install"** — first build for a project downloads its full
  `node_modules`; subsequent builds reuse the shared `npm-cache`/`gradle-cache`
  volumes and are much faster.
- **"eas build --local failed" / credential errors** — the EAS engine needs
  `EXPO_TOKEN` (a real Expo access token) either in `.env` or entered per-build; it
  also expects the project's `eas.json` profile to be otherwise valid.
- **AAB isn't accepted by the Play Store** — make sure you built with **Release**
  signing and a real upload keystore, not the debug default.

## Development

```bash
cd orchestrator && npm install && npm run dev      # Fastify + tsx watch, port 4001
cd expo-builder-gui && npm install && npm run dev  # Next.js dev server, port 3000
```

Run `NEXT_PUBLIC_ORCHESTRATOR_URL=http://localhost:4001 npm run dev` for the GUI if
the orchestrator isn't on the default port. Building the runner image is unaffected by
either dev server — use `make build-image` as usual.
