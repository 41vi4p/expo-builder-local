# expo-builder-local — Project Instructions

## 🏛️ Overview

**expo-builder-local** is a self-hosted, Dockerized build system that turns any
managed Expo (SDK 56+) project into a signed Android APK/AAB from a web GUI — live
logs, resource-usage charts, phase progress + ETA, and a post-build metrics report
(size, duration, version, SHA-256). It is its own top-level project (own git repo:
`git@github.com:41vi4p/expo-builder-local.git`), included here as a submodule/tool
alongside CanteenApp, CRCEats-CS, and CanteenApp_Admin, but it does not build *them*
specifically — point it at any Expo project root.

See [README.md](./README.md) for architecture, setup, and usage. This file covers
**how to work on expo-builder-local itself**: structure, and version/changelog rules.

## 📁 Structure

```
expo-builder-local/
├── CLAUDE.md              ← you are here
├── README.md              ← setup, usage, architecture, security notes
├── docker-compose.yml     ← local-dev path: wires up web + orchestrator; builds the runner image
├── install.sh             ← one-line CLI installer (prefers the hosted APT repo, else .deb/tarball)
├── .env.example
├── Makefile
├── docs/
│   ├── CHANGELOG.md              ← version history for this tool (see below)
│   ├── RELEASING.md              ← release process: what the workflow does, how to cut a tag
│   ├── APT_REPO_SETUP_GUIDE.md   ← one-time: generate the GPG signing key, enable GitHub Pages
│   ├── DOCKER.md                 ← one-time: Docker Hub access token + repo secrets for docker-publish.yml
│   └── apt/pubkey.gpg            ← committed once APT_REPO_SETUP_GUIDE.md is done (public key only)
├── .github/workflows/
│   ├── release.yml        ← "Build and publish APT repository": tag-triggered, builds the .deb
│   │                         inside a pinned ubuntu:24.04, assembles + GPG-signs a real APT repo
│   │                         tree, publishes it to gh-pages/apt, also attaches the .deb to a
│   │                         GitHub Release as a direct-download fallback
│   ├── docker-publish.yml ← "Build and publish Docker images": tag-triggered, matrix over the 3
│   │                         images (runner/orchestrator/web), linux/amd64 only (no QEMU/multi-arch
│   │                         — the runner's Android SDK download would be slow+untested under
│   │                         emulation), pushed via docker/build-push-action
│   └── ci.yml              ← push/PR: just `npm run build` in expo-builder-gui — deliberately
│                              minimal, no CLI/orchestrator build check
├── scripts/
│   └── publish-images.sh  ← build (and optionally push) the 3 Docker Hub images by hand
├── docker/runner/         ← Android toolchain image (Node 22 LTS + JDK 17 + SDK + eas-cli)
│   ├── Dockerfile
│   ├── docker-entrypoint.sh   (UID/GID re-homing)
│   ├── build-entrypoint.sh    (the actual build: prebuild/eas → gradle → collect)
│   └── scripts/               (signing helpers: patch-android-signing.js, write-eas-credentials.js)
├── orchestrator/          ← backend: Fastify + dockerode + better-sqlite3 + ws
│   └── src/{routes,docker,build,store,ws,util}/
├── expo-builder-gui/      ← frontend: Next.js 16 (App Router, Tailwind v4)
│   ├── docker-entrypoint.sh   (substitutes ORCHESTRATOR_URL into the compiled bundle at container start)
│   └── {app,components,lib}/
└── cli/                   ← standalone `ebl` C++ CLI (no orchestrator/GUI/Node needed)
    ├── CMakeLists.txt      (also defines the .deb package — CPack DEB generator)
    └── src/
        ├── main.cpp                    (subcommand dispatch only)
        ├── commands/                  (build, setup, config, start+stop — one file per subcommand)
        ├── config_store.*, crypto.*, base64.*   (encrypted ~/.config/ebl/config.json)
        └── {docker_client,http_client,json,tar_writer,detect,metrics,runner_context,color}.{hpp,cpp}
```

## 🖥️ CLI package (`cli/`)

A standalone **C++17** binary — command name **`ebl`** (short for "expo-local-builder",
deliberately distinct from the `expo-builder-local` project/repo name) — built with
CMake, depending only on libcurl and OpenSSL, that talks to the Docker Engine API
directly over `/var/run/docker.sock`. No orchestrator, no GUI, no Node.js runtime at
all. Subcommands live under `commands/` (`build.*`, `setup.*`, `config.*`, `start.*`
— the last of these also implements `stop`); `main.cpp` is just dispatch. Shared
building blocks:

- `http_client.*` — thin libcurl wrapper using `CURLOPT_UNIX_SOCKET_PATH` (the same
  mechanism the real `docker` CLI uses to talk to the daemon over its socket) plus a
  plain-TCP `httpGetTcp` used only for polling the orchestrator's health endpoint.
- `json.*` — a small hand-written JSON value/parser/serializer (not a vendored
  library — kept deliberately minimal, just enough for Docker API bodies and
  package.json/eas.json reads).
- `tar_writer.*` — builds an in-memory USTAR archive of `docker/runner/` to POST as
  the build context to `/build`, so `ebl build`/`ebl setup` can build the runner image
  itself when it isn't published yet.
- `docker_client.*` — the actual Engine API calls: one-shot build containers (image
  list/build/pull, volume create, container create/attach/start/wait/remove) *and*
  long-running service containers (`ServiceContainerSpec`, network create, find-by-
  name, running-check) used by `ebl start`/`stop`.
- `config_store.*` — `EblConfig` (projects folder, ports, Docker Hub namespace, Expo
  token, generated orchestrator `MASTER_KEY`) persisted at `~/.config/ebl/config.json`
  (0600); `crypto.*` (AES-256-GCM via OpenSSL) encrypts the two secret fields using a
  machine-local key at `~/.config/ebl/machine.key` (0600, generated on first use) —
  `base64.*` is a small hand-written codec used by both.
- `detect.*` / `metrics.*` — reimplementations of `orchestrator/src/build/detect.ts`
  and `metrics.ts` (same rules, different language) — keep them in sync by hand if
  either changes.
- `runner_context.*` — locates the bundled `docker/runner/` copy at runtime via
  `/proc/self/exe`, so a compiled/installed binary works without the rest of this repo
  present (CMake copies `docker/runner/` into the build dir at configure time; see
  `CMakeLists.txt`).

`attachAndStream` blocks on a libcurl call until the container's output stream closes
— it runs on its own `std::thread` while the main thread starts/waits on the
container (see `commands/build.cpp`); don't collapse that back onto one thread, it'll
deadlock (attach would never return control to let the container start).

`ebl start` launches the orchestrator + web images **directly via the Docker API**
(container names `ebl-orchestrator`/`ebl-web`, network `ebl-network`) — deliberately
not `docker compose`, since an apt/script-installed user won't have this repo checked
out at all. The web image's orchestrator URL is baked in as the literal placeholder
`http://__EBL_ORCHESTRATOR_URL__` at build time and substituted for real at container
*start* by `expo-builder-gui/docker-entrypoint.sh`, reading `ORCHESTRATOR_URL` — this
is what makes one published image work regardless of which port a given user picks;
don't reintroduce a build-time `NEXT_PUBLIC_ORCHESTRATOR_URL` ARG.

## 🔄 Version management

Unlike the three apps under the repo root (which version independently), the
orchestrator, the GUI, and the CLI **always ship together** as one product and share
**one version number** — a build only works when all three are compatible, so
tracking them separately would just invite drift.

- **Canonical source:** `orchestrator/package.json`'s `version`,
  `expo-builder-gui/package.json`'s `version`, and `cli/CMakeLists.txt`'s
  `project(... VERSION x.y.z ...)` — **always bump all three to the same value in the
  same change**, even if a given change only touched one of them.
- **Bump rule (SemVer), applied automatically for every change, however small:**
  - `fix:` / `style:` / `refactor:` / docs/config-only change → **PATCH** (+0.0.1)
  - `feat:` / new endpoint / new component / new capability → **MINOR** (+0.1.0, reset PATCH)
  - Breaking change (API shape, WS message shape, env var rename, DB schema change
    requiring a fresh volume) → **MAJOR** (+1.0.0)
- **After every code change to anything under `expo-builder-local/`:**
  1. Make the change.
  2. Bump all three version fields (they must always match).
  3. Add a new entry **at the top** of `docs/CHANGELOG.md` (format below).
  4. If the change is user-visible or structural, add a short summary to the repo
     root's `/CHANGELOG.md` too (this project's entry in the shared changelog),
     following that file's existing per-app section format.
- This is not optional busywork — do it as part of the same commit/turn as the code
  change, not as a follow-up.

### Changelog entry format (`docs/CHANGELOG.md`)

```markdown
## vX.Y.Z — Short title

**Date:** YYYY-MM-DD
**Type:** Fix | Feature | Enhancement | Refactor | Security

- What changed and why (one line per change)
- Root cause, if it's a bug fix

**Files modified:** `path/to/file.ts`, `path/to/other.tsx`
```

## 🧭 Working notes

- The orchestrator is a Docker *sibling* (talks to the host daemon over the mounted
  socket), not a nested container — see the README's "Path handling" section before
  touching anything path-related in `config.ts`, `docker/runner.ts`, or `build/manager.ts`.
  `ALLOWED_ROOTS` and the compose bind mount for `HOST_PROJECTS_ROOT` must always be
  the identical host path.
- `docker/runner/build-entrypoint.sh` emits a small marker protocol on stdout
  (`@@PHASE:`, `@@PROGRESS:`, `@@ENGINE:`, `@@BUILD_NUMBER:`, `@@ARTIFACT:`,
  `@@ERROR:`) that both `orchestrator/src/build/progress.ts`/`manager.ts` (GUI path)
  and `cli/src/commands/build.cpp` (CLI path) parse independently — if you add a new
  build phase, marker, or change engine behavior, update **both** consumers, plus the
  phase weight tables in `progress.ts` and the phase sequence in
  `expo-builder-gui/components/BuildTimeline.tsx`.
- Every build's artifact lands in `<project>/ebl_builds/v<app-version>-build<n>/` —
  `n` comes from `ebl_builds/.build-counter`, a bare-integer file
  `build-entrypoint.sh` increments itself (not something either the CLI or the
  orchestrator computes) — see its "collect" phase. `ebl_builds/` is added to the
  project's own `.gitignore` on first build (also from `build-entrypoint.sh`, in its
  "setup" phase) — this is a property of the *project being built*, not of
  expo-builder-local's own repo.
- Any value that looks like a secret (keystore passwords, an app's own `.env`/`eas.json`
  values, `EXPO_TOKEN`) must stay covered by `orchestrator/src/util/redact.ts` — when
  adding a new source of secret material, add it to the redactor's input list in
  `build/manager.ts`, don't assume it's already covered.
- `expo-builder-gui/lib/types.ts` is a deliberate plain duplicate of
  `orchestrator/src/types.ts` (the two services deploy independently). Keep both in
  sync by hand when either changes.
