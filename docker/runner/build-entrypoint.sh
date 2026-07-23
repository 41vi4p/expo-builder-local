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

# Paths we may write into the *host-mounted* project folder that must never survive
# the build (signing secrets). Always cleaned up, success or failure.
EAS_JSON_BACKUP=""
cleanup() {
  rm -f "${APP_DIR}/android/keystore.properties" \
        "${APP_DIR}/android/app/release.keystore" \
        "${APP_DIR}/credentials.json" 2>/dev/null || true
  if [ -n "${EAS_JSON_BACKUP}" ] && [ -f "${EAS_JSON_BACKUP}" ]; then
    mv -f "${EAS_JSON_BACKUP}" "${APP_DIR}/eas.json"
  fi
}
trap cleanup EXIT

cd "${APP_DIR}" || fail "APP_DIR ${APP_DIR} not found — did the bind mount fail?" 2
[ -f package.json ] || fail "No package.json found at ${APP_DIR} — not a project root" 2
node -e "const p=require('./package.json'); process.exit((p.dependencies&&p.dependencies.expo)||(p.devDependencies&&p.devDependencies.expo)?0:1)" \
  || fail "package.json has no 'expo' dependency — this doesn't look like an Expo project" 2

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
  npm ci --no-audit --no-fund || npm install --no-audit --no-fund --legacy-peer-deps || fail "npm install failed" 1
else
  npm install --no-audit --no-fund --legacy-peer-deps || fail "npm install failed" 1
fi
progress 100

# ---------------------------------------------------------------------------
RESOLVED_ENGINE="${ENGINE}"
if [ "${RESOLVED_ENGINE}" = "auto" ]; then
  if [ -n "${EXPO_TOKEN:-}" ]; then RESOLVED_ENGINE="eas"; else RESOLVED_ENGINE="gradle"; fi
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
  eas build --local --non-interactive --platform android --profile "${PROFILE}" \
    --output "${BUILD_OUTPUT_DIR}/eas-output.${ARTIFACT_TYPE}" \
    || fail "eas build --local failed" 1
  ARTIFACT_PATH="${BUILD_OUTPUT_DIR}/eas-output.${ARTIFACT_TYPE}"

else
  # ---- expo prebuild + Gradle (fully local/offline, no Expo account needed) ----
  phase prebuild "Generating native Android project"
  npx expo prebuild --platform android --clean --non-interactive \
    || fail "expo prebuild failed" 1
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
  (cd "${APP_DIR}/android" && chmod +x ./gradlew && ./gradlew "${GRADLE_TASK}" --console=rich --no-daemon) \
    || fail "gradlew ${GRADLE_TASK} failed" 1

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
