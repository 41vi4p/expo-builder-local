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
├── install.sh             ← one-line CLI installer (APT repo / Arch's PKGBUILD via makepkg / .deb / tarball)
├── .env.example
├── Makefile
├── packaging/
│   └── arch/
│       ├── PKGBUILD       ← builds the CLI from source (makepkg) for Arch-based distros —
│       │                     not published to the AUR yet; see docs/RELEASING.md
│       └── .SRCINFO       ← generated from PKGBUILD (`makepkg --printsrcinfo > .SRCINFO`) —
│                             regenerate it every time PKGBUILD changes, don't hand-edit
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
├── cli/                   ← standalone `ebl` C++ CLI (no orchestrator/GUI/Node needed) —
│   │                         builds natively for both Linux/macOS and Windows from
│   │                         this one source tree (platform branches via CMake +
│   │                         `#ifdef _WIN32`, not a fork)
│   ├── CMakeLists.txt      (also defines the .deb package — CPack DEB generator, Linux-only)
│   └── src/
│       ├── main.cpp                    (subcommand dispatch only)
│       ├── commands/                  (build, setup, config, start+stop — one file per subcommand)
│       ├── config_store.*, crypto.*, base64.*   (encrypted config.json — ~/.config/ebl/ on
│       │                                          Linux/macOS, %APPDATA%\ebl\ on Windows)
│       ├── prompt.*                    (promptString/promptInt/promptHidden — shared by config.cpp's wizard and build.cpp's missing-token prompt)
│       ├── http_client.hpp             (shared HttpClient interface — talks to the Docker
│       │                                 Engine API over its local transport)
│       ├── http_client_unix.cpp        (Linux/macOS: libcurl's CURLOPT_UNIX_SOCKET_PATH
│       │                                 against /var/run/docker.sock)
│       ├── http_client_win.cpp         (Windows: hand-rolled HTTP/1.1 framing over
│       │                                 Docker Desktop's \\.\pipe\docker_engine named
│       │                                 pipe — libcurl has no Windows-npipe transport)
│       ├── http_client_common.cpp      (httpGetTcp/urlEncode — plain TCP, shared by both)
│       ├── winpath.*                   (Windows-only path→Docker-bind-mount translation,
│       │                                 e.g. "D:\App" → "//d/App"; identity elsewhere)
│       ├── native_process.*            (Windows-only: shared Job-Object-based process
│       │                                 runner — wall-clock + idle-CPU timeouts —
│       │                                 used by native_toolchain.* and native_build.*)
│       ├── native_toolchain.*          (Windows-only: detects/provisions the native
│       │                                 engine's JDK/Android SDK/Node — see the
│       │                                 Native Windows build engine section below)
│       ├── native_build.*              (Windows-only: the native engine itself — the
│       │                                 direct analog of build-entrypoint.sh, see below)
│       └── {docker_client,json,tar_writer,detect,metrics,runner_context,color}.{hpp,cpp}
└── windows/               ← Windows-specific packaging only — ebl.exe itself is just
                              `cli/` built for Windows (see above), not a separate binary
    ├── install.ps1         (one-line installer: -Mode Native|Docker (default Native)
    │                         — Docker Desktop presence check + WSL2 tuning in Docker
    │                         mode only; downloads+extracts the ebl.exe release archive,
    │                         puts ebl.exe's bin/ dir on PATH, then runs
    │                         `ebl setup --runtime <Mode>` either way)
    ├── uninstall.ps1       (always removes the install dir + PATH entry; interactively
    │                         offers to also remove the native toolchain
    │                         (%LOCALAPPDATA%\ebl\toolchain\...) and/or saved config
    │                         (%APPDATA%\ebl) — skipped entirely with -Quiet, which is
    │                         what the GUI uninstaller's hidden [UninstallRun] passes)
    └── installer/
        └── ebl.iss         (Inno Setup script → ebl-setup.exe; bundles the same
                              `cmake --install`ed bin/+share/ tree plus the two .ps1
                              files above; its [Code] section adds a wizard page
                              choosing Native vs Docker, threaded into install.ps1
                              via -Mode — see GetInstallModeArg)
```

## 🖥️ CLI package (`cli/`)

A standalone **C++17** binary — command name **`ebl`** (short for "expo-local-builder",
deliberately distinct from the `expo-builder-local` project/repo name) — built with
CMake, depending only on libcurl and OpenSSL, that talks to the Docker Engine API
directly: over `/var/run/docker.sock` on Linux/macOS, or Docker Desktop's
`\\.\pipe\docker_engine` named pipe on Windows. No orchestrator, no GUI, no Node.js
runtime at all. Subcommands live under `commands/` (`build.*`, `setup.*`, `config.*`,
`start.*` — the last of these also implements `stop`); `main.cpp` is just dispatch.
Shared building blocks:

- `http_client.hpp` / `http_client_unix.cpp` / `http_client_win.cpp` /
  `http_client_common.cpp` — same `HttpClient` interface, platform-specific transport:
  `http_client_unix.cpp` is a thin libcurl wrapper using `CURLOPT_UNIX_SOCKET_PATH`
  (the same mechanism the real `docker` CLI uses on Linux/macOS); `http_client_win.cpp`
  hand-rolls HTTP/1.1 request/response framing over `CreateFileW`/`ReadFile`/
  `WriteFile` on the named pipe, since libcurl has no Windows-npipe transport —
  supports both `Content-Length` and chunked bodies, since Docker streams `/build`/
  `/containers/{id}/attach` chunked. `http_client_common.cpp` holds the
  platform-independent `httpGetTcp` (used only for polling the orchestrator's health
  endpoint) and `urlEncode`, compiled on every platform.
- `winpath.*` — Windows-only host-path → Docker-bind-mount-path translation
  (`"D:\Projects\App"` → `"//d/Projects/App"`, the same client-side conversion the
  real `docker` CLI performs, since Docker Desktop's daemon runs inside its own Linux
  VM and a raw drive-letter path means nothing to it). Identity function on every
  other platform — call it unconditionally at bind-mount construction sites in
  `docker_client.cpp`/`commands/start.cpp`, no `#ifdef` needed at the call site.
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
- `config_store.*` — `EblConfig` (projects folder, ports, Expo
  token, generated orchestrator `MASTER_KEY`) persisted at `~/.config/ebl/config.json`
  on Linux/macOS (0600) or `%APPDATA%\ebl\config.json` on Windows (no POSIX chmod
  equivalent applied there — relies on per-user profile isolation instead); `crypto.*`
  (AES-256-GCM via OpenSSL) encrypts the two secret fields using a machine-local key
  next to it (`machine.key`, generated on first use) — `base64.*` is a small
  hand-written codec used by both. Its atomic-save step uses `MoveFileExA` with
  `MOVEFILE_REPLACE_EXISTING` on Windows, not `std::rename` — plain C `rename()`
  fails there if the destination already exists, unlike POSIX `rename(2)`.
- `detect.*` / `metrics.*` — reimplementations of `orchestrator/src/build/detect.ts`
  and `metrics.ts` (same rules, different language) — keep them in sync by hand if
  either changes. `metrics.cpp`'s `git` invocation (for commit/branch metadata) is a
  `fork`/`exec`/`pipe` on Linux/macOS and a `CreateProcess` with a redirected pipe on
  Windows — keep both branches in sync if the command or its argument list changes.
- `runner_context.*` — locates the bundled `docker/runner/` copy at runtime via
  `/proc/self/exe` (Linux/macOS) or `GetModuleFileNameA` (Windows), so a
  compiled/installed binary works without the rest of this repo present (CMake copies
  `docker/runner/` into the build dir at configure time; see `CMakeLists.txt`).

Windows-only placeholder pending real-hardware verification: `commands/start.cpp`'s
`HOST_UID`/`HOST_GID` and `commands/build.cpp`'s build-container UID/GID both hardcode
`1000`/`1000` on Windows (no POSIX uid/gid to report) — this is what
`docker/runner/build-entrypoint.sh`'s UID/GID re-homing step consumes, and the actual
value Docker Desktop's file-sharing layer presents bind-mounted host files under
hasn't been confirmed against a real install yet. (This placeholder is specific to
Docker mode — the native engine below has no container/UID concept at all, so it
sidesteps this whole class of problem entirely.)

## 🪟 Native Windows build engine (Docker vs. Native)

Windows is the only platform with two build engines, chosen via `--runtime`
(`ebl setup --runtime <docker|native>`, `ebl build --runtime <docker|native>`) and
persisted in `EblConfig::buildMode`. **Native is the Windows default** — installs
the Android SDK/JDK/Node directly on the host instead of using Docker Desktop/WSL2
at all, since that VM layer is the single biggest source of Windows friction (WSL2
memory tuning, Docker Desktop licensing/install, crashed Engine API under memory
pressure). Docker remains fully available and is what Linux/macOS always use — this
whole section and everything it describes is **Windows-only**; on every other
platform `buildMode` is always `"docker"` and none of this code even compiles
(`native_process.cpp`/`native_toolchain.cpp`/`native_build.cpp` are only added to
`cli/CMakeLists.txt`'s sources under `if(WIN32)`).

**⚠️ UNVERIFIED ON REAL WINDOWS HARDWARE.** This entire subsystem was written with
no Windows machine available to build or run it on — it compiles cleanly as part of
the Windows-only CMake source list (never exercised, since this sandbox can't
target Windows), the two `.ps1` scripts and the Inno Setup `[Code]` wizard page were
checked with PowerShell's own parser (`[System.Management.Automation.Language.Parser]::ParseFile`)
and reasoned through carefully against documented Win32/Inno Setup APIs, but none of
it has actually run for real. Treat it the same as the `BUILD_UID`/`BUILD_GID`
placeholder above: plausible and carefully reasoned, not proven. Report anything
that doesn't work.

- **`native_toolchain.*`** — `provisionNativeToolchain()`, called from
  `commands/setup.cpp`'s `--runtime native` branch. Detects an existing JDK 17/
  Android SDK/Node install first (env vars / `where node`) and reuses it untouched;
  downloads anything missing into an ebl-owned, isolated
  `%LOCALAPPDATA%\ebl\toolchain\{jdk17,android-sdk,node}` (JDK: Adoptium Temurin;
  Android SDK: **same pinned versions as `docker/runner/Dockerfile`'s ARGs** — keep
  both in sync if those ever change; Node: official nodejs.org zip, `kNodeVersion`
  in `native_toolchain.cpp` is a hand-pinned snapshot to bump periodically, since
  there's no rolling-LTS channel to point at the way the Dockerfile's
  `setup_lts.x` has). Each component's `EblConfig::NativeToolchainConfig` flag
  (`jdkInstalledByEbl` etc.) is per-component and load-bearing — it's what lets
  `windows/uninstall.ps1` later remove exactly what ebl downloaded and never touch
  something the user already had.
- **`native_build.*`** — `runNativeBuild()`, the direct analog of
  `docker/runner/build-entrypoint.sh`, called from `commands/build.cpp`'s
  `--runtime native` branch instead of the whole `DockerClient` create/start/
  attach/wait/remove sequence. Mirrors that script's phases, env-var contract, and
  **byte-for-byte the same marker protocol** (`@@PHASE:`/`@@PROGRESS:`/`@@ENGINE:`/
  `@@BUILD_NUMBER:`/`@@ARTIFACT:`/`@@DURATION:`/`@@ERROR:`) through the exact same
  `onChunk` callback shape `DockerClient::attachAndStream` uses — this is what lets
  `commands/build.cpp`'s existing marker-line parser and success/failure reporting
  work completely unchanged regardless of which engine ran. Reuses
  `docker/runner/scripts/patch-android-signing.js`/`write-eas-credentials.js`
  **unmodified** (bundled to `share/expo-builder-local/native-scripts/` — see
  `CMakeLists.txt`'s `NATIVE_SCRIPTS_SRC_DIR`/`resolveNativeScriptsDir()`) via the
  provisioned/detected Node, rather than reimplementing Gradle-file patching in
  C++. Two whole categories of the Docker path's complexity simply don't exist
  here and were deliberately *not* ported: `docker-entrypoint.sh`'s UID/GID
  re-homing, and the git `safe.directory` bind-mount-ownership workaround — both
  are pure Docker/Linux-bind-mount artifacts, moot when the build runs as the real
  invoking user directly against the real filesystem. **Known v1 simplification**:
  Gradle runs with `--console=plain`, not `--console=rich` — no way to verify
  ConPTY↔Gradle TTY detection from a non-Windows sandbox, so native builds only get
  phase-level 0/100 progress, not Docker mode's live "NN% EXECUTING" bar.
- **`native_process.*`** — shared by both of the above: `runProcessWithTimeout`
  (flat wall-clock ceiling) and `runProcessWithIdleTimeout` (kills only if CPU time
  hasn't advanced for N seconds *or* a hard ceiling is hit) — the Windows Job-Object
  based analog of `build-entrypoint.sh`'s `timeout --foreground`/`run_with_idle_timeout`
  pgrep+ps polling loop. A Job Object can `TerminateJobObject` an entire process
  tree atomically, so — unlike the bash version's `kill_tree`, which has to signal
  each descendant individually to avoid killing its own monitor loop via a shared
  process group — there's no equivalent hazard to work around here.
- If you add a new build phase/marker/env var to `build-entrypoint.sh`, this makes
  **three** independent consumers to keep in sync by hand (was two): the GUI path
  (`orchestrator/src/build/progress.ts`/`manager.ts`), the CLI's Docker path
  (`commands/build.cpp`'s marker parser), and now `native_build.cpp`.
- `windows/install.ps1`'s `-Mode Native|Docker` (default `Native`) and
  `windows/installer/ebl.iss`'s `[Code]` wizard page (`GetInstallModeArg`) are the
  two places a user actually picks a mode; both just end up calling
  `ebl setup --runtime <mode>`, which is the only place that persists the choice
  (`EblConfig::buildMode`) — there's no separate installer-side manifest file.
  `windows/uninstall.ps1` reads that same `config.json` (via PowerShell's built-in
  `ConvertFrom-Json`, no new dependency) to decide what it can safely offer to clean
  up.

## 🌿 Windows-branch workflow

Windows-native-engine work (everything in the section above, plus the `windows/`
packaging scripts) accumulates on a dedicated long-lived `windows` git branch, not
`main` — check `git branch --show-current` before starting further work here.
`ebl_landing_page/` and the root `README.md`/`docs/CHANGELOG.md` are **main-branch-
deploy-authoritative**: the landing page only goes live from `main`, so doc/
landing-page edits made on the `windows` branch are fine to include (keeps the
branch mergeable/complete) but won't be live for real users until the branch is
actually merged — don't treat them as already deployed.

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
orchestrator, the GUI, and the CLI (which now includes the Windows build — `ebl.exe`
is `cli/` compiled for Windows, not a separate binary) **always ship together** as
one product and share **one version number** — a build only works when all of them
are compatible, so tracking them separately would just invite drift.

- **Canonical source:** `orchestrator/package.json`'s `version`,
  `expo-builder-gui/package.json`'s `version`, `cli/CMakeLists.txt`'s
  `project(... VERSION x.y.z ...)`, `windows/installer/ebl.iss`'s
  `MyAppVersion`, and `packaging/arch/PKGBUILD`'s `pkgver` — **always bump all five
  to the same value in the same change**, even if a given change only touched one
  of them. (`PKGBUILD`'s `pkgrel` is separate — see below.)
- **Bump rule (SemVer), applied automatically for every change, however small:**
  - `fix:` / `style:` / `refactor:` / docs/config-only change → **PATCH** (+0.0.1)
  - `feat:` / new endpoint / new component / new capability → **MINOR** (+0.1.0, reset PATCH)
  - Breaking change (API shape, WS message shape, env var rename, DB schema change
    requiring a fresh volume) → **MAJOR** (+1.0.0)
  - A change that only touches `packaging/arch/PKGBUILD` itself (e.g. a `depends=`
    fix) and doesn't otherwise change what gets built → bump `pkgrel` instead of
    `pkgver`, same as any other PKGBUILD.
- **After every code change to anything under `expo-builder-local/`:**
  1. Make the change.
  2. Bump all five version fields (they must always match).
  3. Regenerate `packaging/arch/.SRCINFO` (`makepkg --printsrcinfo > .SRCINFO` from
     `packaging/arch/`) — it's derived from `PKGBUILD` and goes stale the moment
     `pkgver`/`pkgrel`/deps change; never hand-edit it.
  4. Add a new entry **at the top** of `docs/CHANGELOG.md` (format below).
- This is not optional busywork — do it as part of the same commit/turn as the code
  change, not as a follow-up.
- Do **not** add entries to the repo root's `/CHANGELOG.md` for expo-builder-local
  changes — that file is for the three apps under the repo root, not this project.

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
  `@@ERROR:`) parsed independently by `orchestrator/src/build/progress.ts`/
  `manager.ts` (GUI path), `cli/src/commands/build.cpp`'s Docker-path parser (CLI
  path), and — Windows-only — `cli/src/native_build.cpp`, which *emits* those same
  markers itself rather than parsing them (see the Native Windows build engine
  section above). If you add a new build phase, marker, or change engine behavior,
  update all **three**, plus the phase weight tables in `progress.ts` and the phase
  sequence in `expo-builder-gui/components/BuildTimeline.tsx`.
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
- `docker/runner/Dockerfile`'s `npm install -g npm@latest eas-cli@latest` layer only
  ever re-resolves "latest" when that layer's build cache is actually invalidated —
  its `EAS_CLI_CACHE_BUST` build arg is what forces that on every real publish (see
  the Dockerfile's own comment). `.github/workflows/docker-publish.yml` and
  `scripts/publish-images.sh` both pass a fresh value on every run; local dev builds
  (`Makefile`, `docker-compose.yml`) deliberately don't, so they keep normal
  layer-cache reuse. `cli/src/commands/update.cpp` (`ebl update`) sidesteps this arg
  entirely instead, via `DockerClient::buildImage`'s `noCache` param (Docker's
  `nocache`+`pull` build options) — keep both mechanisms in mind if this ever needs
  changing, they solve the same staleness problem for two different callers (CI vs.
  a user's own machine).
