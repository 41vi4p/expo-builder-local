# expo-builder-local — Changelog

Version history for the orchestrator + GUI (versioned together — see
[../CLAUDE.md](../CLAUDE.md#-version-management)). Most recent first.

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
