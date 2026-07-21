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
├── docker-compose.yml     ← wires up web + orchestrator; builds the runner image
├── .env.example
├── Makefile
├── docs/
│   └── CHANGELOG.md       ← version history for this tool (see below)
├── docker/runner/         ← Android toolchain image (Node 22 LTS + JDK 17 + SDK + eas-cli)
│   ├── Dockerfile
│   ├── docker-entrypoint.sh   (UID/GID re-homing)
│   ├── build-entrypoint.sh    (the actual build: prebuild/eas → gradle → collect)
│   └── scripts/               (signing helpers: patch-android-signing.js, write-eas-credentials.js)
├── orchestrator/          ← backend: Fastify + dockerode + better-sqlite3 + ws
│   └── src/{routes,docker,build,store,ws,util}/
└── expo-builder-gui/      ← frontend: Next.js 16 (App Router, Tailwind v4)
    └── {app,components,lib}/
```

## 🔄 Version management

Unlike the three apps under the repo root (which version independently), the
orchestrator and the GUI **always ship together** as one product and share **one
version number** — a build only works when both are compatible, so tracking them
separately would just invite drift.

- **Canonical source:** `orchestrator/package.json` `version` and
  `expo-builder-gui/package.json` `version` — **always bump both to the same value in
  the same change.**
- **Bump rule (SemVer), applied automatically for every change, however small:**
  - `fix:` / `style:` / `refactor:` / docs/config-only change → **PATCH** (+0.0.1)
  - `feat:` / new endpoint / new component / new capability → **MINOR** (+0.1.0, reset PATCH)
  - Breaking change (API shape, WS message shape, env var rename, DB schema change
    requiring a fresh volume) → **MAJOR** (+1.0.0)
- **After every code change to anything under `expo-builder-local/`:**
  1. Make the change.
  2. Bump both `package.json` versions (they must always match).
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
  (`@@PHASE:`, `@@PROGRESS:`, `@@ENGINE:`, `@@ARTIFACT:`, `@@ERROR:`) that
  `orchestrator/src/build/progress.ts` and `manager.ts` parse — if you add a new build
  phase or change engine behavior, update the phase weight tables in `progress.ts` and
  the phase sequence in `expo-builder-gui/components/BuildTimeline.tsx` together.
- Any value that looks like a secret (keystore passwords, an app's own `.env`/`eas.json`
  values, `EXPO_TOKEN`) must stay covered by `orchestrator/src/util/redact.ts` — when
  adding a new source of secret material, add it to the redactor's input list in
  `build/manager.ts`, don't assume it's already covered.
- `expo-builder-gui/lib/types.ts` is a deliberate plain duplicate of
  `orchestrator/src/types.ts` (the two services deploy independently). Keep both in
  sync by hand when either changes.
