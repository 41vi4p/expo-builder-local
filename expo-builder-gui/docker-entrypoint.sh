#!/bin/sh
# Next.js inlines NEXT_PUBLIC_* variables into the client bundle at *build* time, but
# a Docker-Hub-published image is built once and run by many different people, each
# potentially choosing a different orchestrator port. So the build bakes in a fixed
# placeholder (http://__EBL_ORCHESTRATOR_URL__) instead of a real URL, and this
# entrypoint substitutes the real value — from the ORCHESTRATOR_URL env var set at
# `docker run`/`ebl start` time — into every compiled JS file before the server starts.
set -eu

ORCHESTRATOR_URL="${ORCHESTRATOR_URL:-http://localhost:4001}"

grep -rl '__EBL_ORCHESTRATOR_URL__' /app/.next /app/server.js 2>/dev/null | while IFS= read -r file; do
  sed -i "s|http://__EBL_ORCHESTRATOR_URL__|${ORCHESTRATOR_URL}|g" "$file"
done

exec node server.js
