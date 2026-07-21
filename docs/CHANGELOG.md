# expo-builder-local — Changelog

Version history for the orchestrator + GUI (versioned together — see
[../CLAUDE.md](../CLAUDE.md#-version-management)). Most recent first.

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
