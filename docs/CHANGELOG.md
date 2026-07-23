# expo-builder-local — Changelog

Version history for the orchestrator + GUI (versioned together — see
[../CLAUDE.md](../CLAUDE.md#-version-management)). Most recent first.

## v0.4.1 — Fix: `.deb` build missing `file` in the pinned container

**Date:** 2026-07-23
**Type:** Fix

- The first real run of the v0.4.0 release workflow failed: `CPackDeb: file utility
  is not available. CPACK_DEBIAN_PACKAGE_SHLIBDEPS and
  CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS options are not available.` CPack's DEB
  generator shells out to the `file` command to auto-detect runtime library
  dependencies (`libcurl4t64`, `libssl3t64`, etc.) — present on this host (where it
  had been validated manually) but not in a minimal `ubuntu:24.04` Docker base image,
  which is what CI actually builds in.
- Fix: added `file` to the `apt-get install` line in `.github/workflows/release.yml`'s
  build step. Reproduced the exact failure and the fix in a fresh `ubuntu:24.04`
  container locally (not just inferred from the error message) before pushing.
- The `v0.4.0` tag already points at the broken commit; tags aren't moved once
  published (no force-push), so this ships as a fresh `v0.4.1` tag instead of
  retagging `v0.4.0`.

**Files modified:** `.github/workflows/release.yml`; `orchestrator/package.json`,
`expo-builder-gui/package.json`, `cli/CMakeLists.txt` (version bump).

## v0.4.0 — Signed APT repository via GitHub Pages

**Date:** 2026-07-23
**Type:** Feature

- **Real, signed APT repository**, not just a downloadable `.deb`: `.github/
  workflows/release.yml` (renamed "Build and publish APT repository") now assembles a
  proper repo tree (`pool/`, `dists/stable/main/binary-amd64/{Packages,Packages.gz}`)
  and GPG-signs the `Release` file (`Release.gpg` + `InRelease`) the way `apt` itself
  verifies — checked automatically on every install once a user has added the repo,
  not just an optional manual `dpkg-sig --verify`. Published to the `gh-pages` branch
  under `/apt` via `peaceiris/actions-gh-pages`, served at
  `https://41vi4p.github.io/expo-builder-local/apt`. Supersedes the previous
  `dpkg-sig`-signed-single-`.deb` approach from v0.3.0.
- The `.deb` is still built with our existing CMake+CPack pipeline (not switched to
  `fpm`/Ruby) — kept `dpkg-shlibdeps` auto-detecting runtime deps — but now runs
  inside a pinned `ubuntu:24.04` container in CI, matching the host this was
  validated against locally, for reproducibility.
- The workflow still also creates a GitHub Release with the `.deb` + a plain tarball +
  `SHA256SUMS.txt` attached, as a direct-download fallback for anyone who'd rather not
  add the repo.
- `install.sh` now prefers adding the hosted repo (checks the repo is actually live
  first) over downloading a one-off `.deb`, so installs default to the path that also
  gets future releases via a plain `apt upgrade`; falls back to the old direct-
  download behavior if the repo isn't reachable yet or `apt` isn't available.
- New `docs/APT_REPO_SETUP_GUIDE.md`: the one-time setup this all depends on —
  generating a *passphrase-less* signing key (deliberately, to avoid fragile
  non-interactive-passphrase CI plumbing), registering `APT_GPG_PRIVATE_KEY` as a repo
  secret, committing the public half at `docs/apt/pubkey.gpg`, enabling GitHub Pages.
  `docs/RELEASING.md` slimmed down to focus on cutting a release, pointing here for
  the prerequisite setup instead of duplicating it.
- The workflow fails fast (`::error::`) if `docs/apt/pubkey.gpg` or the
  `APT_GPG_PRIVATE_KEY` secret aren't present yet, rather than silently publishing an
  unsigned or inconsistently-signed repo.

**Files modified:** `.github/workflows/release.yml` (rewritten); new
`docs/APT_REPO_SETUP_GUIDE.md`, new `docs/apt/README.md` (placeholder for the
user-supplied `pubkey.gpg`); `docs/RELEASING.md` (slimmed, points to the setup guide);
`install.sh` (apt-repo-first logic); `README.md` (Quick start + new "APT repository"
section + troubleshooting entries); `CLAUDE.md`; `orchestrator/package.json`,
`expo-builder-gui/package.json`, `cli/CMakeLists.txt` (version bump).

## v0.3.0 — Docker Hub distribution, CLI subcommands, `ebl_builds/` versioning

**Date:** 2026-07-22
**Type:** Feature

- **CLI restructured into subcommands**: `ebl setup`, `ebl config`, `ebl start`,
  `ebl stop`, `ebl build` (the previous bare `ebl [path]` behavior). `main.cpp` is now
  just dispatch; each subcommand lives under `cli/src/commands/`.
- **`ebl setup`**: checks whether Docker is installed and reachable; if not, offers
  (with confirmation) to install it via the official `get.docker.com` convenience
  script, then pulls the runner/orchestrator/web images.
- **`ebl config`**: interactive wizard for the projects folder, Expo access token
  (read with terminal echo disabled), orchestrator/web ports, and Docker Hub
  namespace. Saved to `~/.config/ebl/config.json` (0600); the token and a generated
  orchestrator `MASTER_KEY` are AES-256-GCM-encrypted using a machine-local key at
  `~/.config/ebl/machine.key` (0600, generated on first use) — new `crypto.*`/
  `base64.*`/`config_store.*` modules.
- **`ebl start`/`ebl stop`**: run the orchestrator + web GUI as Docker containers
  directly via the Engine API (`docker_client.*` gained `pullImage`, `ensureNetwork`,
  `ServiceContainerSpec`/`createServiceContainer`, name-based lookup) — deliberately
  not `docker compose`, so an apt/script-installed user never needs this repo checked
  out. Polls both services' health endpoints (new `httpGetTcp` in `http_client.*`)
  and reports online/not-responding for each.
- **Runtime-configurable web image**: `expo-builder-gui`'s orchestrator URL used to be
  baked in at Docker build time (`NEXT_PUBLIC_ORCHESTRATOR_URL` ARG), which can't work
  for a published image used by people on different ports. Now the build bakes in a
  literal placeholder (`http://__EBL_ORCHESTRATOR_URL__`) and a new
  `docker-entrypoint.sh` substitutes the real value from `ORCHESTRATOR_URL` into the
  compiled bundle at container *start*. `docker-compose.yml`'s `web` service updated
  to match (env var instead of build arg).
- **`ebl_builds/` versioned build folders**: `build-entrypoint.sh` now writes each
  artifact to `ebl_builds/v<app-version>-build<n>/` inside the project, where `n` is a
  simple per-project counter (`ebl_builds/.build-counter`) — every build gets a
  stable, human-referenceable number regardless of how many times a given app version
  gets rebuilt. `ebl_builds/` is auto-added to the project's `.gitignore` on first
  build. New `@@BUILD_NUMBER:` marker consumed by both the CLI and the orchestrator/
  GUI (`buildNumber` field threaded through `types.ts`, `db.ts`, `manager.ts`,
  `MetricsPanel.tsx`). Previously this was `build-output/` with a timestamp-only name.
- **Docker Hub images**: all three images (`runner`, `orchestrator`, `web`) now build
  under a configurable namespace (default placeholder `ebllocal` — set your own via
  `ebl config` or `DOCKERHUB_NAMESPACE`), matching between `docker-compose.yml`,
  `cli/src/config_store.hpp`'s defaults, and the new `scripts/publish-images.sh`
  (build + optional `--push`, refuses to push under the placeholder namespace). `ebl
  build`/`ebl setup`/`ebl start` all prefer pulling from the registry, falling back to
  a local build only for the runner image (orchestrator/web have no local-build
  fallback — they're meant to be pre-published).
- **Signed `.deb` release pipeline**: `cli/CMakeLists.txt` gained CPack DEB packaging
  (dependencies auto-detected via `dpkg-shlibdeps`, not hand-pinned — verified this
  correctly picks up `libcurl4t64`/`libssl3t64` on this Ubuntu version rather than the
  older `libcurl4`/`libssl3` names). New `.github/workflows/release.yml`, triggered on
  `v*` tags: builds, packages, GPG-signs via `dpkg-sig` (skips signing gracefully if
  `GPG_PRIVATE_KEY`/`GPG_KEY_ID` secrets aren't set), and publishes a GitHub Release
  with the `.deb`, a plain tarball (for `install.sh`), and `SHA256SUMS.txt`. New
  `docs/RELEASING.md` documents generating/registering the signing key end to end.
- **One-line installer** (`install.sh`): prefers `apt install ./ebl_*.deb` (resolves
  `libcurl4`/`libssl3` automatically) when `dpkg` is present, else extracts the plain
  tarball into `~/.local` (or `/usr/local` as root) — no package manager required
  either way.
- **Fixed a real, current build break**: `node:lts-alpine` (used by `orchestrator/
  Dockerfile` and `expo-builder-gui/Dockerfile`) now resolves to Node 24, for which
  `better-sqlite3` has no prebuilt binary yet — its from-source `node-gyp` rebuild
  failed fetching headers in this environment. Pinned both to `node:22-alpine`
  (a specific, currently well-supported LTS) instead of the rolling tag.
- Verified end-to-end against the real Docker daemon (not just compiled): built both
  the orchestrator and web images for real, ran `ebl start` against them, confirmed
  both health checks pass, confirmed the `__EBL_ORCHESTRATOR_URL__` placeholder is
  fully substituted in the served bundle, and separately tested `ebl setup`'s graceful
  failure path (pull returns 404 under the unpublished placeholder namespace).

**Files modified:** `cli/src/main.cpp` (rewritten as dispatch), new
`cli/src/commands/{build,setup,config,start}.{hpp,cpp}`, new
`cli/src/{config_store,crypto,base64}.{hpp,cpp}`, `cli/src/docker_client.*`
(pull/network/service-container additions), `cli/src/http_client.*` (`httpGetTcp`),
`cli/CMakeLists.txt` (CPack DEB config, version bump); `docker/runner/
build-entrypoint.sh` (`ebl_builds/` versioning + counter + gitignore); `orchestrator/
src/{types,store/db,build/manager}.ts` (`buildNumber`), `expo-builder-gui/lib/types.ts`
+ `components/MetricsPanel.tsx` (`buildNumber` display); `expo-builder-gui/Dockerfile`
+ new `docker-entrypoint.sh` (runtime URL substitution); `orchestrator/Dockerfile`,
`expo-builder-gui/Dockerfile` (Node 22 pin); `docker-compose.yml`, `.env.example`
(`DOCKERHUB_NAMESPACE`, `ORCHESTRATOR_URL`); new `scripts/publish-images.sh`, new
`.github/workflows/release.yml`, new `docs/RELEASING.md`, new `install.sh`;
`Makefile` (`deb`, `publish-images`, `publish-images-push` targets); `README.md`
(full rewrite around the new CLI-first quick start); `CLAUDE.md`.

## v0.2.2 — Fix: bare `ebl` invocation

**Date:** 2026-07-21
**Type:** Fix

- Running `ebl` with no arguments at all defaulted `path` to `.` and attempted a real
  build of the current directory — if that directory wasn't an Expo project (e.g. the
  developer just typed `ebl` from inside `expo-builder-local/` itself to see what it
  does), this surfaced as a confusing "doesn't look like an Expo project" error
  instead of anything resembling help.
- Fix: a bare `ebl` (argc == 1) now prints usage and exits 0, matching how
  git/docker/kubectl behave with zero arguments. `ebl .` (explicit) and `ebl <path>`
  are unaffected — the default path "." still applies once any argument is given.

**Files modified:** `cli/src/main.cpp`; `orchestrator/package.json`,
`expo-builder-gui/package.json`, `cli/CMakeLists.txt` (version bump).

## v0.2.1 — Fix: CLI build directory

**Date:** 2026-07-21
**Type:** Fix

- `make install-cli` built CMake's output directly in `cli/build/`. On a checkout
  living on a slow or contended filesystem (observed: an `ntfs3`-mounted drive with
  concurrent filesystem operations against overlapping paths), this could make
  configure/build appear to hang indefinitely rather than just being slow — CMake's
  own many-small-file writes (object files, compiler feature-detection tests) hit the
  same contention as `npm install`/`rm -rf` do on such mounts.
- Root cause: no relationship to the CLI's own code — purely a "where do build
  artifacts land" problem for any out-of-tree CMake build.
- Fix: `make install-cli` now builds in `~/.cache/expo-builder-local/cli-build`
  (overridable via `CLI_BUILD_DIR`) instead of inside the repo, so the build always
  lands on whatever filesystem the developer's home directory is on — sidestepping
  the problem entirely rather than trying to fix the underlying filesystem behavior.
  Verified end-to-end after the fix: configure+build+install completed in seconds.

**Files modified:** `Makefile` (`install-cli` target, `CLI_BUILD_DIR` variable);
`orchestrator/package.json`, `expo-builder-gui/package.json`, `cli/CMakeLists.txt`
(version bump).

## v0.2.0 — Standalone CLI (C++)

**Date:** 2026-07-21
**Type:** Feature

- Added `cli/`: a standalone command, `ebl` (short for "expo-local-builder"; `ebl .`)
  that talks to the Docker Engine API directly over `/var/run/docker.sock` — no
  orchestrator/GUI process, and (deliberately) no Node.js runtime at all. Built in
  C++17 with CMake, depending only on libcurl (HTTP-over-unix-socket) and OpenSSL
  (SHA-256); JSON parsing/serialization and the build-context tar writer are small
  hand-written implementations rather than vendored libraries.
- Supports the same artifact/profile/engine/signing options as the GUI, plus
  `--expo-token`/`EXPO_TOKEN` and keystore flags (prefer `EXPO_BUILDER_STORE_PASSWORD`/
  `EXPO_BUILDER_KEY_PASSWORD` env vars over CLI flags for passwords).
- Container output streams straight through to the terminal with a real TTY (so
  Gradle's `--console=rich` progress bar renders exactly as it would locally); a
  parallel, non-destructive line scan picks the `@@ENGINE:`/`@@ARTIFACT:`/`@@ERROR:`
  markers out of the same stream for a build summary (size, duration, version,
  SHA-256) on exit. Attaching to the container's output is a blocking libcurl call
  that only returns once the container exits, so it runs on its own thread while the
  main thread starts/waits on the container — collapsing that onto one thread would
  deadlock (attach would never return control to start the container).
- Auto-builds the runner image on first use if it isn't already present — CMake
  copies `docker/runner/` into the build directory at configure time, and the
  compiled binary locates it at runtime via `/proc/self/exe` (falling back to the
  `cmake --install` layout, or an `EXPO_BUILDER_RUNNER_DIR` override), so it works
  even if only the compiled executable is copied elsewhere.
- Shares the same Gradle/npm cache Docker volumes as GUI-triggered builds (same
  default volume names), so switching between CLI and GUI builds is cheap. On the
  host UID/GID front the CLI is simpler than the orchestrator: running natively (not
  inside a container), `getuid()/getgid()` already are the real host IDs, so there's
  no `HOST_UID`/`HOST_GID` configuration needed for this path.
- CLI builds are intentionally not recorded in the GUI's SQLite build history — the
  two are independent by design (see `README.md`).
- Bumped `orchestrator`, `expo-builder-gui`, and `cli` (via `CMakeLists.txt`'s
  `project(... VERSION ...)`) to v0.2.0 together (all three share one version per
  this project's convention — see `../CLAUDE.md`).

**Files modified:** initial creation of `cli/` (`CMakeLists.txt`, `src/*.{hpp,cpp}`);
`orchestrator/package.json`, `expo-builder-gui/package.json` (version bump);
`Makefile` (`install-cli` target, CMake-based); `README.md` (CLI usage +
build-prerequisites section); `CLAUDE.md` (version management now covers three
packages, not two).

## v0.1.0 — Initial build system

**Date:** 2026-07-21
**Type:** Feature

- Initial implementation of the whole tool: pick any Expo project folder in a web GUI,
  build a signed Android APK/AAB in a disposable Docker container, and watch it happen
  live.
- `docker/runner`: Ubuntu 24.04 + Node LTS + JDK 17 + Android SDK/NDK + `eas-cli` image.
  `build-entrypoint.sh` supports two engines — `expo prebuild` + Gradle (fully local, no
  Expo account) and `eas build --local` (uses the project's own `eas.json` profiles) —
  with an `auto` mode that prefers EAS only when an `EXPO_TOKEN` is supplied.
- Release signing via an uploaded keystore: `patch-android-signing.js` wires a
  `release` signing config into the generated `android/app/build.gradle` for the
  Gradle engine; `write-eas-credentials.js` writes a temporary local `credentials.json`
  + `eas.json` override for the EAS engine. Both clean up after themselves, win or lose.
- `orchestrator`: Fastify + dockerode backend that spawns/supervises build containers,
  streams stdout and `docker stats` over a per-build WebSocket, parses Gradle's
  `--console=rich` live percentage (and our own `@@PHASE`/`@@PROGRESS` markers) into a
  blended progress/ETA, persists history to SQLite, and redacts every value it can find
  in an app's own `.env`/`eas.json` (plus `EXPO_TOKEN` and keystore passwords) from all
  logs before they're stored or streamed.
- `expo-builder-gui`: Next.js 16 dashboard — directory browser with Expo-project
  detection, build config form, a phase-timeline progress view, an xterm.js live log
  panel (so Gradle's carriage-return progress bar renders correctly), CPU/memory/
  network/disk charts (Recharts, validated categorical palette per the dataviz skill),
  a post-build metrics panel (size + delta, duration, version, SHA-256), and a build
  history page with a size-over-time trend.
- Security: keystore passwords AES-256-GCM-encrypted at rest; Docker socket access
  documented as root-equivalent; both services bind to `127.0.0.1` by default.

**Files modified:** initial creation of `docker/runner/`, `orchestrator/`,
`expo-builder-gui/`, `docker-compose.yml`, `.env.example`, `Makefile`, `README.md`.
