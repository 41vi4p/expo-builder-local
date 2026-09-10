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
│   ├── release.yml        ← "Build and publish APT repository": tag-triggered, builds a static
│   │                         (CGO_ENABLED=0) `ebl` with go build + nfpm, assembles + GPG-signs a
│   │                         real APT repo tree, publishes it to gh-pages/apt, also attaches the
│   │                         .deb to a GitHub Release as a direct-download fallback; a second job
│   │                         builds ebl.exe for Windows and the Inno Setup installer
│   ├── docker-publish.yml ← "Build and publish Docker images": tag-triggered, matrix over the 3
│   │                         images (runner/orchestrator/web), linux/amd64 only (no QEMU/multi-arch
│   │                         — the runner's Android SDK download would be slow+untested under
│   │                         emulation), pushed via docker/build-push-action
│   └── ci.yml              ← push/PR: `npm run build` in expo-builder-gui, `cli/` builds + vets +
│                              tests (`go build`/`go vet`/`go test ./...`) natively on both Linux
│                              and Windows, and windows/installer/ebl.iss compiles against a
│                              locally-built ebl.exe — deliberately still no orchestrator build
│                              check, no macOS job
├── scripts/
│   ├── bump-version.sh    ← bumps the one shared version number everywhere it lives
│   │                         in one shot (see Version management below) — use this,
│   │                         don't hand-edit the version fields
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
├── cli/                   ← standalone `ebl` Go CLI (no orchestrator/GUI/Node needed) —
│   │                         builds natively for both Linux/macOS and Windows from
│   │                         this one module (platform branches via Go build tags/
│   │                         `_unix.go`/`_windows.go` files, not a fork)
│   ├── go.mod / go.sum      (module github.com/41vi4p/expo-builder-local/cli — 3 deps
│   │                         total: golang.org/x/term, golang.org/x/sys,
│   │                         github.com/Microsoft/go-winio; everything else is stdlib)
│   ├── VERSION              (canonical version source — see Version management below;
│   │                         read via `-ldflags -X main.version=$(cat VERSION)` at
│   │                         build time, cmd/ebl/main.go's `var version` is just the
│   │                         dev-build fallback)
│   ├── scripts/
│   │   └── sync-runner-assets.sh   (copies ../docker/runner/ into
│   │                                 internal/runnerctx/assets/runner/ so //go:embed
│   │                                 can bundle it — Go embed can't reference paths
│   │                                 outside its own package dir. Run before every
│   │                                 build/test/release; that assets/ dir is gitignored)
│   ├── cmd/ebl/
│   │   ├── main.go          (subcommand dispatch only — direct port of the old main.cpp)
│   │   ├── versioninfo.json (goversioninfo config — FixedFileInfo/StringFileInfo the
│   │   │                     Windows exe embeds)
│   │   ├── ebl.ico           (multi-res icon generated from ../../docs/assets/ebl_logo.png
│   │   │                      — same source ebl_landing_page/app/favicon.ico uses;
│   │   │                      regenerate both by hand if that logo ever changes, not
│   │   │                      auto-synced)
│   │   └── resource_windows_amd64.syso   (generated, not hand-written — regenerate via
│   │                                       main.go's `//go:generate goversioninfo`
│   │                                       comment after editing versioninfo.json/ebl.ico)
│   └── internal/            (one package per concern, unit-tested with `go test ./...`)
│       ├── commands/                  (build, create, setup, config, start+stop, update,
│       │                                clean, completion — one file per subcommand.
│       │                                completion.go's four shell scripts are
│       │                                hand-written against each other subcommand's
│       │                                actual flags, not generated from a shared
│       │                                table — update them by hand if flags ever change)
│       ├── config/                    (encrypted config.json — ~/.config/ebl/ on
│       │                                Linux/macOS, %APPDATA%\ebl\ on Windows, via
│       │                                os.UserConfigDir())
│       ├── cryptoutil/                 (AES-256-GCM via crypto/aes+cipher, encrypts
│       │                                config's secret fields using a machine-local
│       │                                key next to it — machine.key, generated on
│       │                                first use)
│       ├── prompt/                    (promptString/promptInt/promptHidden — shared by
│       │                                config's wizard and build's missing-token
│       │                                prompt; hidden input via golang.org/x/term)
│       ├── dockertransport/           (net/http.Transport with a Unix-socket dialer
│       │                                — transport_unix.go — and, transport_windows.go,
│       │                                a go-winio named-pipe dialer for
│       │                                \\.\pipe\docker_engine; one http.Client either way)
│       ├── dockerapi/                  (containers.go: one-shot build containers, image
│       │                                list/build/pull, volumes, attach/start/wait/
│       │                                remove; service.go: long-running service
│       │                                containers for `ebl start`/`stop` — network
│       │                                create, find-by-name, running-check;
│       │                                dockerapi.go: ping/ContainerStats poller for
│       │                                `ebl build --status`'s dashboard — see
│       │                                dockerstats/ for the actual frame-parsing)
│       ├── winpath/                    (Windows-only path→Docker-bind-mount translation,
│       │                                e.g. "D:\App" → "//d/App"; identity elsewhere)
│       ├── updatecheck/                (GitHub releases-API check on `ebl --version`,
│       │                                rate-limited to once/24h via a cached
│       │                                update-check.json next to config; version
│       │                                comparison itself lives in versioncompare/ for
│       │                                unit-testability)
│       ├── dockerstats/                (parses a raw /containers/{id}/stats frame into
│       │                                CPU%/memory — same formula
│       │                                orchestrator/src/docker/stats.ts uses; pure
│       │                                logic, unit-tested, same as versioncompare/)
│       ├── buildstatusview/             (`ebl build --status`'s live dashboard:
│       │                                redraw-in-place via ANSI cursor movement, same
│       │                                technique pullprogress/ uses for one line,
│       │                                extended to a whole block. Plain ASCII
│       │                                sparklines only, deliberately - no Unicode
│       │                                block characters, so this renders correctly on
│       │                                a legacy Windows console codepage with no
│       │                                global UTF-8 console-output change needed.
│       │                                Verified against real pty output — see
│       │                                testharness/)
│       ├── tuiinput/                   (single raw keypress reads, normalized to
│       │                                Up/Down/Enter/Escape/Other, built on
│       │                                golang.org/x/term's raw mode on every
│       │                                platform — no separate Windows _getch() path
│       │                                needed anymore. Verified via a real pty-driven
│       │                                test — tuiinput_pty_test.go — not just
│       │                                synthetic byte-feeding)
│       ├── tuimenu/                    (`ebl build --tui`'s arrow-key single-select
│       │                                menu, built on tuiinput/ + the same
│       │                                redraw-in-place technique as buildstatusview/.
│       │                                Also verified via a real pty-driven test)
│       ├── hostprocess/                (os/exec-based streaming subprocess runner —
│       │                                a large simplification over the old manual
│       │                                fork/exec/pipe and CreateProcess/CreatePipe
│       │                                branches)
│       ├── metrics/                    (reimplementation of metrics.ts's artifact
│       │                                metrics — same rules, different language, keep
│       │                                in sync by hand if either changes; its `git`
│       │                                invocation for commit/branch metadata is a
│       │                                plain os/exec call on every platform now)
│       ├── detect/                     (reimplementation of detect.ts's project-type
│       │                                detection — same rules, different language)
│       ├── hostinfo/                   (OS version string — hostinfo_windows.go via
│       │                                golang.org/x/sys/windows/registry,
│       │                                hostinfo_unix.go via /etc/os-release + uname)
│       ├── runnerctx/                  (bundled docker/runner/ via //go:embed — see
│       │                                assets/ and scripts/sync-runner-assets.sh above;
│       │                                replaces the old runtime self-exe-relative
│       │                                lookup entirely, so an installed binary needs
│       │                                nothing else present on disk)
│       ├── tarctx/                     (builds an in-memory USTAR archive of
│       │                                docker/runner/ via archive/tar, to POST as the
│       │                                build context to /build so `ebl build`/`ebl
│       │                                setup` can build the runner image itself when
│       │                                it isn't published yet)
│       ├── pullprogress/                (redraw-in-place single-line progress for
│       │                                image pulls, same ANSI technique as
│       │                                buildstatusview/ applied to one line)
│       ├── color/                      (ANSI + TTY detection)
│       └── versioncompare/             (isVersionNewer() — pure logic, used by
│                                        updatecheck/)
└── windows/               ← Windows-specific packaging only — ebl.exe itself is just
                              `cli/` built for Windows (see above), not a separate binary
    ├── install.ps1         (one-line installer: Docker Desktop presence check,
    │                         downloads+extracts the ebl.exe release archive — a single
    │                         static binary, docker/runner/ is embedded in it — puts
    │                         ebl.exe's bin/ dir on PATH)
    ├── uninstall.ps1
    └── installer/
        └── ebl.iss         (Inno Setup script → ebl-setup.exe; bundles the built
                              ebl.exe plus the two .ps1 files above, and just runs
                              install.ps1 -LocalInstallDir — no separate install
                              logic of its own)
```

## 🖥️ CLI package (`cli/`)

A standalone **Go** binary (module `github.com/41vi4p/expo-builder-local/cli`) —
command name **`ebl`** (short for "expo-local-builder", deliberately distinct from
the `expo-builder-local` project/repo name) — that talks to the Docker Engine API
directly: over `/var/run/docker.sock` on Linux/macOS, or Docker Desktop's
`\\.\pipe\docker_engine` named pipe on Windows. No orchestrator, no GUI, no Node.js
runtime at all. Subcommands live under `internal/commands/` (`build.go`, `setup.go`,
`config.go`, `start.go` — the last of these also implements `stop`); `cmd/ebl/main.go`
is just dispatch. Only three non-stdlib dependencies, all effectively
extensions-of-the-standard-library maintained by Go/Microsoft themselves:
`golang.org/x/term` (raw terminal mode, hidden password input, TTY detection),
`golang.org/x/sys` (Windows registry access, console mode), and
`github.com/Microsoft/go-winio` (the Windows named-pipe dialer — used internally by
Docker and Kubernetes for exactly this problem). Shared building blocks:

- `internal/dockertransport/` — one `http.Client`, platform-specific dialer:
  `transport_unix.go` dials the Unix socket directly (`net.Dial("unix", ...)`, the
  same mechanism the real `docker` CLI uses on Linux/macOS); `transport_windows.go`
  plugs `go-winio`'s named-pipe dialer into `http.Transport.DialContext` for
  `\\.\pipe\docker_engine` — `net/http` handles `Content-Length` and chunked bodies
  either way, since Docker streams `/build`/`/containers/{id}/attach` chunked.
- `internal/winpath/` — Windows-only host-path → Docker-bind-mount-path translation
  (`"D:\Projects\App"` → `"//d/Projects/App"`, the same client-side conversion the
  real `docker` CLI performs, since Docker Desktop's daemon runs inside its own Linux
  VM and a raw drive-letter path means nothing to it). Identity function on every
  other platform — call it unconditionally at bind-mount construction sites in
  `dockerapi/`/`commands/start.go`, no build-tag branch needed at the call site.
- JSON bodies (Docker API, package.json/eas.json reads) use `encoding/json` directly
  — no hand-rolled parser to maintain anymore.
- `internal/tarctx/` — builds an in-memory USTAR archive of `docker/runner/` via
  `archive/tar` to POST as the build context to `/build`, so `ebl build`/`ebl setup`
  can build the runner image itself when it isn't published yet.
- `internal/dockerapi/` — the actual Engine API calls: `containers.go` for one-shot
  build containers (image list/build/pull, volume create, container create/attach/
  start/wait/remove), `service.go` for long-running service containers (network
  create, find-by-name, running-check) used by `ebl start`/`stop`; also a one-shot
  `GET /containers/{id}/stats?stream=false` poller for `ebl build --status`'s live
  CPU/memory dashboard — see `internal/dockerstats/` for the actual frame-parsing logic.
- `internal/config/` — the config struct (projects folder, ports, Expo token,
  generated orchestrator `MASTER_KEY`) persisted at `~/.config/ebl/config.json` on
  Linux/macOS (0600) or `%APPDATA%\ebl\config.json` on Windows (no POSIX chmod
  equivalent applied there — relies on per-user profile isolation instead), via
  `os.UserConfigDir()`; `internal/cryptoutil/` (AES-256-GCM via `crypto/aes`+
  `crypto/cipher`) encrypts the two secret fields using a machine-local key next to it
  (`machine.key`, generated on first use) — `encoding/base64` covers the codec need
  directly, no hand-written wrapper. The atomic-save step is a plain `os.Rename()` on
  every platform — it already does the `MOVEFILE_REPLACE_EXISTING` equivalent
  internally on Windows, unlike C's `rename()`, so no platform branch is needed here
  either.
- `internal/detect/` / `internal/metrics/` — reimplementations of
  `orchestrator/src/build/detect.ts` and `metrics.ts` (same rules, different
  language) — keep them in sync by hand if either changes. `metrics.go`'s `git`
  invocation (for commit/branch metadata) is a single `os/exec.Command` call on every
  platform now — no separate fork/exec vs. CreateProcess branches needed.
- `internal/runnerctx/` — bundles the `docker/runner/` copy directly into the binary
  via `//go:embed` (see `internal/runnerctx/assets/`, populated by
  `scripts/sync-runner-assets.sh` before every build since embed can't reference
  `../docker/runner` outside the package dir), so a compiled/installed binary works
  without the rest of this repo present at runtime — no self-exe-relative lookup
  needed anymore.

Windows-only placeholder pending real-hardware verification:
`internal/commands/build_uidgid_windows.go`'s (and `start.go`'s) build-container
UID/GID both hardcode `1000`/`1000` on Windows (no POSIX uid/gid to report) — this is
what `docker/runner/build-entrypoint.sh`'s UID/GID re-homing step consumes, and the
actual value Docker Desktop's file-sharing layer presents bind-mounted host files
under hasn't been confirmed against a real install yet.

The container's attach stream blocks on an HTTP call until the container's output
stream closes — it runs on its own goroutine while the main goroutine starts/waits on
the container (see `internal/commands/build_run.go`); don't collapse that onto one
goroutine, it'll deadlock (attach would never return control to let the container
start). Cancellation (Ctrl-C) uses `os/signal.Notify` plus a `buildFinished` channel,
replacing the old `volatile sig_atomic_t` + watcher-thread pattern.

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

- **Use `scripts/bump-version.sh` to bump the version — don't hand-edit the version
  fields.** It's the single source of truth for *how* to bump; don't re-derive the
  mechanics below by reading each target file.
  ```bash
  scripts/bump-version.sh patch   # or: minor | major | an explicit X.Y.Z
  ```
  In one shot it updates `orchestrator/package.json`'s `version`,
  `expo-builder-gui/package.json`'s `version`, `cli/VERSION`,
  `windows/installer/ebl.iss`'s `MyAppVersion`, `packaging/arch/PKGBUILD`'s `pkgver`
  (and resets `pkgrel` to `1`), `cli/cmd/ebl/versioninfo.json`'s version fields, and
  regenerates both `cli/cmd/ebl/resource_windows_amd64.syso` (needs `go` + network —
  warns and skips if unavailable, regenerate by hand later) and
  `packaging/arch/.SRCINFO` (via local `makepkg` if present, else a throwaway
  `archlinux:latest` Docker container — warns and skips if neither is available).
  It refuses to run if the requested version equals the current one.
  **It does not touch `docs/CHANGELOG.md`** (that entry's content can't be
  generated) or `packaging/arch/PKGBUILD`'s `sha256sums` (a real checksum of the
  tagged source archive, only computable after tagging — see
  `docs/RELEASING.md`'s "Arch Linux packaging" section) - do both by hand.
- **Bump rule (SemVer), applied automatically for every change, however small:**
  - `fix:` / `style:` / `refactor:` / docs/config-only change → **PATCH** (+0.0.1)
  - `feat:` / new endpoint / new component / new capability → **MINOR** (+0.1.0, reset PATCH)
  - Breaking change (API shape, WS message shape, env var rename, DB schema change
    requiring a fresh volume) → **MAJOR** (+1.0.0)
  - A change that only touches `packaging/arch/PKGBUILD` itself (e.g. a `depends=`
    fix) and doesn't otherwise change what gets built → bump `pkgrel` by hand instead
    of running the script (`pkgver` doesn't move, so re-running `.SRCINFO`'s
    generation is the only other step needed).
- **After every code change to anything under `expo-builder-local/`:**
  1. Make the change.
  2. Run `scripts/bump-version.sh <patch|minor|major|X.Y.Z>` per the SemVer rule above.
  3. Add a new entry **at the top** of `docs/CHANGELOG.md` (format below).
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
  `@@ERROR:`) that both `orchestrator/src/build/progress.ts`/`manager.ts` (GUI path)
  and `cli/internal/commands/build_run.go` (CLI path) parse independently — if you add a new
  build phase, marker, or change engine behavior, update **both** consumers, plus the
  phase weight tables in `progress.ts` and the phase sequence in
  `expo-builder-gui/components/BuildTimeline.tsx`. On the CLI side, `@@ENGINE:`/
  `@@ARTIFACT:`/`@@ERROR:`/`@@BUILD_NUMBER:` are always parsed; `@@PHASE:`/
  `@@PROGRESS:` are only actually *used* when `--status` is passed (they drive
  `BuildStatusView`'s live dashboard - see below). Default (non-`--status`) mode
  still echoes every marker line as literal text in the raw streamed log - a
  pre-existing cosmetic wart, deliberately left alone rather than risk changing
  already-shipped default output while adding `--status`. `--status` itself never
  shows this, since it suppresses the raw passthrough entirely in favor of the
  dashboard.
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
  layer-cache reuse. `internal/commands/update.go` (`ebl update`) sidesteps this arg
  entirely instead, via `dockerapi.Client.BuildImage`'s `noCache` param (Docker's
  `nocache`+`pull` build options) — keep both mechanisms in mind if this ever needs
  changing, they solve the same staleness problem for two different callers (CI vs.
  a user's own machine).
