#!/usr/bin/env bash
#
# Turns the Expo project bind-mounted at $APP_DIR into a signed Android APK/AAB.
#
# Emits a small structured marker protocol on stdout that the orchestrator's
# build/progress.ts parses to drive the live dashboard:
#   @@PHASE:<id>:<label>       a new phase started
#   @@PROGRESS:<0-100>         coarse progress within the current phase
#   @@ENGINE:<eas|gradle>      which engine was actually used (after auto-resolution)
#   @@BUILD_NUMBER:<n>         this project's monotonic build counter, post-increment
#   @@ARTIFACT:<path>          final artifact path (inside the bind-mounted APP_DIR,
#                              under ebl_builds/v<version>-build<n>/)
#   @@DURATION:<seconds>       total wall-clock build time
#   @@ERROR:<message>          fatal error, human-readable
#
# Everything else on stdout/stderr is treated as raw log output and streamed
# verbatim (after redaction — see orchestrator/src/util/redact.ts) to the GUI.

set -eo pipefail

: "${APP_DIR:=/work/app}"
: "${ARTIFACT_TYPE:=apk}"     # apk | aab
: "${PROFILE:=preview}"       # eas.json build profile name
: "${ENGINE:=auto}"           # auto | eas | gradle
: "${SIGNING_MODE:=debug}"    # debug | release
# Bounded time budgets for every step that talks to a registry, a daemon, or does
# real dependency/native-toolchain work — none of these should ever be able to hang
# a build forever. `npm ci` in particular is known to hang (rather than fail fast,
# the way `npm install` does) when it needs to fall into live resolution — e.g. a
# lock file that's drifted out of sync with package.json alongside a real
# peer-dependency conflict — so its own fallback to `npm install` below is useless
# without a timeout forcing the handoff. All overridable via env for unusually large
# projects or slow links.
: "${INSTALL_TIMEOUT:=300}"          # npm ci — 5 min
: "${INSTALL_FALLBACK_TIMEOUT:=600}" # npm install (fallback / no lock file) — 10 min
: "${PREBUILD_TIMEOUT:=300}"         # expo prebuild — 5 min
: "${EAS_BUILD_TIMEOUT:=2400}"       # eas build --local — 40 min
: "${GRADLE_TIMEOUT:=2400}"          # gradlew assemble/bundleRelease — 40 min
: "${MIN_FREE_DISK_MB:=2048}"        # hard-fail below this much free space — 2 GB
SCRIPTS_DIR="/usr/local/lib/expo-builder"
# Scratch space for intermediate engine output (the eas engine's --output target) —
# deliberately NOT on the host bind mount, so nothing but the final ebl_builds/
# folder ever appears in the developer's project directory.
BUILD_OUTPUT_DIR="/tmp/ebl-scratch"
EBL_BUILDS_DIR="${APP_DIR}/ebl_builds"
START_TS=$(date +%s)

phase()    { echo "@@PHASE:$1:$2"; }
progress() { echo "@@PROGRESS:$1"; }
fail()     { echo "@@ERROR:$1"; exit "${2:-1}"; }

# Runs "$@" under a hard wall-clock budget so a hang anywhere downstream (a
# registry, a daemon, a native toolchain) turns into a bounded failure instead of
# blocking the build indefinitely. SIGTERM first, SIGKILL 10s later if that alone
# doesn't take — mirrors what we've observed hung npm/gradle processes actually need
# to die. Exit code 124 means "timed out"; anything else is the wrapped command's own
# exit status, so existing `||` fallback chains keep working unmodified.
run_with_timeout() {
  local seconds="$1"; shift
  timeout --kill-after=10s "${seconds}s" "$@"
}

# A near-full disk was a real contributor to at least one hang we've seen in
# practice (extraction/write syscalls stalling under I/O pressure rather than
# failing) — cheap to check, worth failing on fast rather than discovering it an
# hour into a stalled install. Checks both the project bind mount and /cache (the
# npm/gradle volumes), since they can be, and often are, on different filesystems.
check_disk_space() {
  local path="$1" label="$2" free_mb
  [ -d "${path}" ] || return 0
  free_mb="$(df -Pm "${path}" 2>/dev/null | awk 'NR==2{print $4}')"
  [ -n "${free_mb}" ] || return 0
  if [ "${free_mb}" -lt "${MIN_FREE_DISK_MB}" ]; then
    fail "Only ${free_mb}MB free on ${label} (${path}) — need at least ${MIN_FREE_DISK_MB}MB. Free up disk space (docker system prune is usually the fastest way) and retry." 3
  fi
}

# Paths we may write into the *host-mounted* project folder that must never survive
# the build (signing secrets). Always cleaned up, success or failure.
#
# credentials.json is deliberately NOT in the unconditional list below: unlike
# android/keystore.properties and android/app/release.keystore (which only ever
# exist inside a directory this same run just regenerated via `expo prebuild
# --clean`), credentials.json lives at the project root and a developer may already
# have their own committed there (EAS's own local-credentials format, used whenever
# their eas.json sets credentialsSource: "local") — that's not ours to delete. Only
# remove it if CREDENTIALS_JSON_WRITTEN records that *this run* wrote it (see the
# EAS release-signing block below).
EAS_JSON_BACKUP=""
CREDENTIALS_JSON_WRITTEN=""
cleanup() {
  rm -f "${APP_DIR}/android/keystore.properties" \
        "${APP_DIR}/android/app/release.keystore" 2>/dev/null || true
  if [ -n "${CREDENTIALS_JSON_WRITTEN}" ]; then
    rm -f "${APP_DIR}/credentials.json" 2>/dev/null || true
  fi
  if [ -n "${EAS_JSON_BACKUP}" ] && [ -f "${EAS_JSON_BACKUP}" ]; then
    mv -f "${EAS_JSON_BACKUP}" "${APP_DIR}/eas.json"
  fi
}
trap cleanup EXIT

cd "${APP_DIR}" || fail "APP_DIR ${APP_DIR} not found — did the bind mount fail?" 2

# Docker Desktop's bind-mount for a host path doesn't preserve real host ownership —
# files show up owned by whichever UID its file-sharing layer defaults to (observed:
# root, regardless of BUILD_UID/the actual Windows file owner), which almost never
# matches the re-homed `builder` user this script runs as (see docker-entrypoint.sh).
# That mismatch trips git's post-CVE-2022-24765 "dubious ownership" safety check —
# without this exception, `eas build --local` doesn't surface that error at all, it
# just falls back to a confusing "not a git repository, initialize one?" prompt that
# then hangs forever (non-interactive, no stdin attached).
git config --global --add safe.directory "${APP_DIR}"

[ -f package.json ] || fail "No package.json found at ${APP_DIR} — not a project root" 2
node -e "const p=require('./package.json'); process.exit((p.dependencies&&p.dependencies.expo)||(p.devDependencies&&p.devDependencies.expo)?0:1)" \
  || fail "package.json has no 'expo' dependency — this doesn't look like an Expo project" 2

check_disk_space "${APP_DIR}" "the project directory"
check_disk_space "/cache" "the build cache volume"

mkdir -p "${BUILD_OUTPUT_DIR}"

# ---------------------------------------------------------------------------
phase setup "Preparing project"
# Peer-dep conflicts are endemic across the RN/Expo ecosystem (multiple packages
# pinning slightly different react/react-native ranges); default every project to
# legacy-peer-deps unless it already has its own .npmrc opinion about it.
if [ ! -f .npmrc ]; then
  echo "legacy-peer-deps=true" > .npmrc
fi

# Every build's output lands in ebl_builds/ inside the project — make sure it's
# gitignored from the very first build, so nobody accidentally commits a stack of
# APKs/AABs. Idempotent: only appends the line if it isn't already present.
if [ -f .gitignore ]; then
  grep -qxF "ebl_builds/" .gitignore || echo "ebl_builds/" >> .gitignore
else
  echo "ebl_builds/" > .gitignore
fi
progress 100

# ---------------------------------------------------------------------------
phase install "Installing dependencies"
if [ -f package-lock.json ]; then
  # Deliberately `if CMD; then :; else ...; fi` rather than `if ! CMD; then` — under
  # `set -e`, negating with `!` also rewrites $? for the branch, so the real
  # underlying exit code (124 for a timeout vs. npm's own failure code) would be
  # lost right when we need it most to tell the two apart.
  if run_with_timeout "${INSTALL_TIMEOUT}" npm ci --no-audit --no-fund; then
    :
  else
    ci_status=$?
    if [ "${ci_status}" -eq 124 ]; then
      echo "npm ci didn't finish within ${INSTALL_TIMEOUT}s — most likely package.json and" \
           "package-lock.json have drifted out of sync (e.g. a dependency was added/bumped" \
           "without re-running npm install) combined with a peer-dependency conflict npm" \
           "can't resolve without deciding, which some npm versions hang on instead of" \
           "failing fast. Falling back to npm install, which will re-resolve and regenerate" \
           "the lock file."
    fi
    run_with_timeout "${INSTALL_FALLBACK_TIMEOUT}" npm install --no-audit --no-fund --legacy-peer-deps \
      || fail "npm install failed (or didn't finish within ${INSTALL_FALLBACK_TIMEOUT}s) after npm ci also failed — check for a genuine dependency conflict in package.json" 1
  fi
else
  run_with_timeout "${INSTALL_FALLBACK_TIMEOUT}" npm install --no-audit --no-fund --legacy-peer-deps \
    || fail "npm install failed (or didn't finish within ${INSTALL_FALLBACK_TIMEOUT}s)" 1
fi
progress 100

# ---------------------------------------------------------------------------
# "auto" prefers EAS only when the project actually looks EAS-managed (has an
# eas.json to pull a build profile from) AND a token is available to use it.
# Requiring eas.json (not just a token) matters now that a token often resolves
# even for projects that never touch EAS — ebl config's default/per-owner tokens
# (see config_store.hpp / CLAUDE.md's Multiple Expo accounts section) mean some
# token being set is no longer a reliable signal that *this* project wants EAS;
# eas.json's presence is a much stronger one. Without it, `eas build --local`
# would just fail anyway (no profile to build).
RESOLVED_ENGINE="${ENGINE}"
if [ "${RESOLVED_ENGINE}" = "auto" ]; then
  if [ -n "${EXPO_TOKEN:-}" ] && [ -f "${APP_DIR}/eas.json" ]; then
    RESOLVED_ENGINE="eas"
  else
    RESOLVED_ENGINE="gradle"
  fi
fi
echo "@@ENGINE:${RESOLVED_ENGINE}"

ARTIFACT_PATH=""

if [ "${RESOLVED_ENGINE}" = "eas" ]; then
  # ---- EAS local build (uses eas.json profiles as-is; credentials remote by default) ----
  [ -n "${EXPO_TOKEN:-}" ] || fail "ENGINE=eas requires an EXPO_TOKEN (Expo access token)" 2
  export EXPO_TOKEN

  if [ "${SIGNING_MODE}" = "release" ] && [ -n "${KEYSTORE_PATH:-}" ]; then
    phase signing "Configuring release signing (EAS local credentials)"
    EAS_JSON_BACKUP="$(mktemp)"
    cp "${APP_DIR}/eas.json" "${EAS_JSON_BACKUP}"
    # Set before invoking the script (not after) so cleanup still removes
    # credentials.json even if the script fails partway through writing it.
    CREDENTIALS_JSON_WRITTEN=1
    node "${SCRIPTS_DIR}/write-eas-credentials.js" \
      --projectDir "${APP_DIR}" \
      --profile "${PROFILE}" \
      --keystore "${KEYSTORE_PATH}" \
      --storePassword "${KEYSTORE_PASSWORD:-}" \
      --keyAlias "${KEY_ALIAS:-}" \
      --keyPassword "${KEY_PASSWORD:-}" \
      || fail "Failed to prepare local EAS credentials" 1
  fi

  phase eas "Building with EAS (local)"
  run_with_timeout "${EAS_BUILD_TIMEOUT}" eas build --local --non-interactive --platform android --profile "${PROFILE}" \
    --output "${BUILD_OUTPUT_DIR}/eas-output.${ARTIFACT_TYPE}" \
    || fail "eas build --local failed (or didn't finish within ${EAS_BUILD_TIMEOUT}s)" 1
  ARTIFACT_PATH="${BUILD_OUTPUT_DIR}/eas-output.${ARTIFACT_TYPE}"

else
  # ---- expo prebuild + Gradle (fully local/offline, no Expo account needed) ----
  phase prebuild "Generating native Android project"
  run_with_timeout "${PREBUILD_TIMEOUT}" npx expo prebuild --platform android --clean --non-interactive \
    || fail "expo prebuild failed (or didn't finish within ${PREBUILD_TIMEOUT}s)" 1
  progress 100

  if [ "${SIGNING_MODE}" = "release" ] && [ -n "${KEYSTORE_PATH:-}" ]; then
    phase signing "Configuring release signing"
    node "${SCRIPTS_DIR}/patch-android-signing.js" \
      --androidDir "${APP_DIR}/android" \
      --keystore "${KEYSTORE_PATH}" \
      --storePassword "${KEYSTORE_PASSWORD:-}" \
      --keyAlias "${KEY_ALIAS:-}" \
      --keyPassword "${KEY_PASSWORD:-}" \
      || fail "Failed to configure release signing — check android/app/build.gradle manually" 1
  fi

  phase gradle "Compiling (Gradle)"
  GRADLE_TASK="assembleRelease"
  OUT_GLOB="${APP_DIR}/android/app/build/outputs/apk/release/*.apk"
  if [ "${ARTIFACT_TYPE}" = "aab" ]; then
    GRADLE_TASK="bundleRelease"
    OUT_GLOB="${APP_DIR}/android/app/build/outputs/bundle/release/*.aab"
  fi

  # --console=rich requires a TTY (the orchestrator allocates one via dockerode's
  # Tty:true) and gives a live "NN% EXECUTING" progress line that build/progress.ts
  # parses for the dashboard's progress bar + ETA. Falls back gracefully to plain
  # output if stdout isn't actually a TTY (e.g. when running this script by hand).
  (cd "${APP_DIR}/android" && chmod +x ./gradlew && run_with_timeout "${GRADLE_TIMEOUT}" ./gradlew "${GRADLE_TASK}" --console=rich --no-daemon) \
    || fail "gradlew ${GRADLE_TASK} failed (or didn't finish within ${GRADLE_TIMEOUT}s)" 1

  # shellcheck disable=SC2086
  FOUND="$(ls -1 ${OUT_GLOB} 2>/dev/null | head -n1)"
  [ -n "${FOUND}" ] || fail "Build reported success but no ${ARTIFACT_TYPE} was found under android/app/build/outputs" 1
  ARTIFACT_PATH="${FOUND}"
fi

# ---------------------------------------------------------------------------
phase collect "Collecting artifact"
APP_NAME="$(node -pe "require('./package.json').name || 'app'")"
APP_VERSION="$(node -pe "require('./package.json').version || '0.0.0'")"

# A simple monotonic build counter, scoped to this project (not the app's own
# version) — every build gets a unique, human-referenceable "build N", regardless of
# how many times a given app version gets rebuilt. Stored as a bare integer so it's
# trivial to read/bump without a JSON dependency in this shell script.
mkdir -p "${EBL_BUILDS_DIR}"
COUNTER_FILE="${EBL_BUILDS_DIR}/.build-counter"
PREV_BUILD_NUMBER=0
[ -f "${COUNTER_FILE}" ] && PREV_BUILD_NUMBER="$(cat "${COUNTER_FILE}")"
BUILD_NUMBER=$((PREV_BUILD_NUMBER + 1))
echo "${BUILD_NUMBER}" > "${COUNTER_FILE}"

BUILD_SUBDIR="v${APP_VERSION}-build${BUILD_NUMBER}"
BUILD_DIR="${EBL_BUILDS_DIR}/${BUILD_SUBDIR}"
mkdir -p "${BUILD_DIR}"

FINAL_NAME="${APP_NAME}-${PROFILE}.${ARTIFACT_TYPE}"
FINAL_PATH="${BUILD_DIR}/${FINAL_NAME}"
cp "${ARTIFACT_PATH}" "${FINAL_PATH}"
progress 100
echo "@@BUILD_NUMBER:${BUILD_NUMBER}"
echo "@@ARTIFACT:${FINAL_PATH}"

END_TS=$(date +%s)
echo "@@DURATION:$((END_TS - START_TS))"
phase done "Build complete"
exit 0
