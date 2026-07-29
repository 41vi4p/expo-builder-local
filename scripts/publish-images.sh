#!/usr/bin/env bash
# Builds and (optionally) pushes the three expo-builder-local images to Docker Hub —
# runner, orchestrator, web — all under the same namespace/tag scheme the CLI
# (cli/src/config_store.hpp) and docker-compose.yml expect.
#
# Usage:
#   DOCKERHUB_NAMESPACE=yourusername ./scripts/publish-images.sh          # build only
#   DOCKERHUB_NAMESPACE=yourusername ./scripts/publish-images.sh --push   # build + push
#
# Before --push: `docker login` with an account that has push access to
# DOCKERHUB_NAMESPACE. This script never logs in for you and never pushes without
# --push being passed explicitly — publishing to a shared registry is a real,
# externally-visible action.
set -euo pipefail

DOCKERHUB_NAMESPACE="${DOCKERHUB_NAMESPACE:-ebllocal}"
PUSH=0
if [ "${1:-}" = "--push" ]; then
  PUSH=1
fi

if [ "${DOCKERHUB_NAMESPACE}" = "ebllocal" ] && [ "${PUSH}" = "1" ]; then
  echo "Refusing to push under the placeholder namespace \"ebllocal\"." >&2
  echo "Set DOCKERHUB_NAMESPACE to your real Docker Hub username/org first." >&2
  exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

RUNNER_TAG="${DOCKERHUB_NAMESPACE}/expo-builder-local-runner:latest"
ORCHESTRATOR_TAG="${DOCKERHUB_NAMESPACE}/expo-builder-local-orchestrator:latest"
WEB_TAG="${DOCKERHUB_NAMESPACE}/expo-builder-local-web:latest"

echo "==> Building ${RUNNER_TAG} (this one's large — Android SDK — expect ~10-20 min)"
docker build -t "${RUNNER_TAG}" docker/runner

echo "==> Building ${ORCHESTRATOR_TAG}"
docker build -t "${ORCHESTRATOR_TAG}" orchestrator

echo "==> Building ${WEB_TAG}"
docker build -t "${WEB_TAG}" expo-builder-gui

if [ "${PUSH}" = "1" ]; then
  echo "==> Pushing all three images to Docker Hub..."
  docker push "${RUNNER_TAG}"
  docker push "${ORCHESTRATOR_TAG}"
  docker push "${WEB_TAG}"
  echo "==> Published under ${DOCKERHUB_NAMESPACE}."
else
  echo "==> Built locally, not pushed. Re-run with --push when you're ready (after 'docker login')."
fi
