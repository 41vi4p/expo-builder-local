#!/usr/bin/env bash
# Re-homes the built-in "builder" user to the host UID/GID passed by the orchestrator
# (BUILD_UID / BUILD_GID env vars) so that files written into bind-mounted host
# directories (the app folder, npm/gradle caches) end up owned by the host developer,
# not by a random container UID. Falls back to the image default (1000:1000) if unset.
set -euo pipefail

TARGET_UID="${BUILD_UID:-1000}"
TARGET_GID="${BUILD_GID:-1000}"

CURRENT_UID="$(id -u builder)"
CURRENT_GID="$(id -g builder)"

UID_CHANGED=0
if [ "${TARGET_GID}" != "${CURRENT_GID}" ]; then
  groupmod -o -g "${TARGET_GID}" builder
  UID_CHANGED=1
fi
if [ "${TARGET_UID}" != "${CURRENT_UID}" ]; then
  usermod -o -u "${TARGET_UID}" builder
  UID_CHANGED=1
fi

# Only re-chown persistent cache/work dirs when the UID/GID actually moved — these
# volumes grow large across builds (node_modules, gradle cache) and a full recursive
# chown on every container start would otherwise waste minutes per build.
if [ "${UID_CHANGED}" = "1" ]; then
  chown -R builder:builder /cache /work /keystores 2>/dev/null || true
fi

exec gosu builder "$@"
